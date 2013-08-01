/* RFA consumer.
 *
 * One single consumer, and hence wraps a RFA session for simplicity.
 * Connection events (7.4.7.4, 7.5.8.3) are ignored as they're completely
 * useless.
 *
 * Definition of overlapping terms:
 *   OMM Consumer:  Underlying RFA consumer object.
 *   Consumer:      Application encapsulation of consumer functionality.
 *   Session:       RFA session object that contains one or more "Connection"
 *                  objects for horizontal scaling, e.g. RDF, GARBAN, TOPIC3.
 *   Connection:    RFA connection object that contains one or more servers.
 *   Server List:   A list of servers with round-robin failover connectivity.
 */

#include "consumer.hh"

#include <algorithm>
#include <utility>

/* Protocol Buffers */
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/gzip_stream.h>

#include "chromium/logging.hh"
#include "chromium/string_util.hh"
#include "error.hh"
#include "rfaostream.hh"

#include <archive.pb.h>

using rfa::common::RFA_String;

/* Reuters Wire Format nomenclature for dictionary names. */
static const RFA_String kRdmFieldDictionaryName ("RWFFld");
static const RFA_String kEnumTypeDictionaryName ("RWFEnum");

torikuru::consumer_t::consumer_t (
	const torikuru::session_config_t& config,
	std::shared_ptr<torikuru::rfa_t> rfa,
	std::shared_ptr<rfa::common::EventQueue> event_queue,
	google::protobuf::io::CodedOutputStream* coded_stream
	) :
	last_activity_ (boost::posix_time::microsec_clock::universal_time()),
	config_ (config),
	rfa_ (rfa),
	event_queue_ (event_queue),
	coded_stream_ (coded_stream),
	disable_update_ (false),
	disable_refresh_ (false),
	refresh_count_ (0),
	in_sync_ (false),
	rwf_major_version_ (0),
	rwf_minor_version_ (0),
	is_muted_ (true)
{
	memset (cumulative_stats_, 0, sizeof (cumulative_stats_));
	memset (snap_stats_, 0, sizeof (snap_stats_));
}

torikuru::consumer_t::~consumer_t()
{
	VLOG(3) << "Unregistering RFA session clients.";
	item_handle_.reset();
	error_item_handle_.reset();
	omm_consumer_.reset();
	market_data_subscriber_.reset();
	session_.reset();
}

bool
torikuru::consumer_t::Init (
	bool disable_update,
	bool disable_refresh,
	bool interest_after_refresh,
	std::function<void()>& on_sync
	)
throw (rfa::common::InvalidConfigurationException, rfa::common::InvalidUsageException)
{
	last_activity_ = boost::posix_time::microsec_clock::universal_time();

	disable_update_ = disable_update;
	disable_refresh_ = disable_refresh;
	interest_after_refresh_ = interest_after_refresh;

	on_sync_ = on_sync;

/* 7.2.1 Configuring the Session Layer Package.
 */
	VLOG(3) << "Acquiring RFA session.";
	const RFA_String sessionName (config_.session_name.c_str(), 0, false);
	session_.reset (rfa::sessionLayer::Session::acquire (sessionName));
	if (!(bool)session_)
		return false;

/* 6.2.2.1 RFA Version Info.  The version is only available if an application
 * has acquired a Session (i.e., the Session Layer library is loaded).
 */
	LOG(INFO) << "RFA: { \"productVersion\": \"" << rfa::common::Context::getRFAVersionInfo()->getProductVersion() << "\" }";

	if (LowerCaseEqualsASCII (config_.protocol, connections::kRSSL))
	{
/* 7.5.6 Initializing an OMM Non-Interactive Provider. */
		VLOG(3) << "Creating OMM consumer.";
		const RFA_String consumerName (config_.consumer_name.c_str(), 0, false);
		omm_consumer_.reset (session_->createOMMConsumer (consumerName, nullptr));
		if (!(bool)omm_consumer_)
			return false;

/* 7.5.7 Registering for Events from an OMM Non-Interactive Provider. */
/* receive error events (OMMCmdErrorEvent) related to calls to submit(). */
		VLOG(3) << "Registering OMM error interest.";	
		rfa::sessionLayer::OMMErrorIntSpec ommErrorIntSpec;
		error_item_handle_.reset (omm_consumer_->registerClient (event_queue_.get(), &ommErrorIntSpec, *this, nullptr /* closure */),
					  [this](rfa::common::Handle* handle) { omm_consumer_->unregisterClient (handle); });
		if (!(bool)error_item_handle_)
			return false;
		
		return SendLoginRequest();
	}
	else if (LowerCaseEqualsASCII (config_.protocol, connections::kSSLED))
	{
/* 9.4.2.1 Initializing Market Data Subscriber */
		VLOG(3) << "Creating market data subscriber.";
		const RFA_String subscriberName (config_.consumer_name.c_str(), 0, false);
		market_data_subscriber_.reset (session_->createMarketDataSubscriber (subscriberName));
		if (!(bool)market_data_subscriber_)
			return false;

		VLOG(3) << "Registering market data status interest.";
		rfa::sessionLayer::MarketDataSubscriberInterestSpec marketDataSubscriberInterestSpec;
		error_item_handle_.reset (market_data_subscriber_->registerClient (*event_queue_.get(), marketDataSubscriberInterestSpec, *this, nullptr /* closure*/),
					  [this](rfa::common::Handle* handle) { market_data_subscriber_->unregisterClient (*handle); });
		if (!(bool)error_item_handle_)
			return false;

		return true;
	}

	LOG(ERROR) << "Unsupported transport protocol \"" << config_.protocol << "\".";
	return false;
}

/* 7.3.5.3 Making a Login Request	
 * A Login request message is encoded and sent by OMM Consumer and OMM non-
 * interactive provider applications.
 */
bool
torikuru::consumer_t::SendLoginRequest()
throw (rfa::common::InvalidUsageException)
{
	VLOG(2) << "Sending login request.";
	rfa::message::ReqMsg request;
	request.setMsgModelType (rfa::rdm::MMT_LOGIN);
	request.setInteractionType (rfa::message::ReqMsg::InitialImageFlag | rfa::message::ReqMsg::InterestAfterRefreshFlag);

	rfa::message::AttribInfo attribInfo;
	attribInfo.setNameType (rfa::rdm::USER_NAME);
	const RFA_String userName (config_.user_name.c_str(), 0, false);
	attribInfo.setName (userName);

/* The request attributes ApplicationID and Position are encoded as an
 * ElementList (5.3.4).
 */
	rfa::data::ElementList elementList;
	rfa::data::ElementListWriteIterator it;
	it.start (elementList);

/* DACS Application Id.
 * e.g. "256"
 */
	rfa::data::ElementEntry element;
	element.setName (rfa::rdm::ENAME_APP_ID);
	rfa::data::DataBuffer elementData;
	const RFA_String applicationId (config_.application_id.c_str(), 0, false);
	elementData.setFromString (applicationId, rfa::data::DataBuffer::StringAsciiEnum);
	element.setData (elementData);
	it.bind (element);

/* DACS Position name.
 * e.g. "localhost"
 */
	element.setName (rfa::rdm::ENAME_POSITION);
	const RFA_String position (config_.position.c_str(), 0, false);
	elementData.setFromString (position, rfa::data::DataBuffer::StringAsciiEnum);
	element.setData (elementData);
	it.bind (element);

/* Instance Id (optional).
 * e.g. "<Instance Id>"
 */
	if (!config_.instance_id.empty()) {
		element.setName (rfa::rdm::ENAME_INST_ID);
		const RFA_String instanceId (config_.instance_id.c_str(), 0, false);
		elementData.setFromString (instanceId, rfa::data::DataBuffer::StringAsciiEnum);
		element.setData (elementData);
		it.bind (element);
	}

	it.complete();
	attribInfo.setAttrib (elementList);
	request.setAttribInfo (attribInfo);

/* 4.2.8 Message Validation.  RFA provides an interface to verify that
 * constructed messages of these types conform to the Reuters Domain
 * Models as specified in RFA API 7 RDM Usage Guide.
 */
	uint8_t validation_status = rfa::message::MsgValidationError;
	try {
		RFA_String warningText;
		validation_status = request.validateMsg (&warningText);
		if (rfa::message::MsgValidationWarning == validation_status)
			LOG(WARNING) << "MMT_LOGIN::validateMsg: { \"warningText\": \"" << warningText << "\" }";
		cumulative_stats_[CONSUMER_PC_MMT_LOGIN_VALIDATED]++;
	} catch (const rfa::common::InvalidUsageException& e) {
		LOG(ERROR) << "InvalidUsageException: { " <<
				   "\"StatusText\": \"" << e.getStatus().getStatusText() << "\""
				", " << request <<
			      " }";
		cumulative_stats_[CONSUMER_PC_MMT_LOGIN_MALFORMED]++;
	} catch (const std::exception& e) {
		LOG(ERROR) << "Rfa::Exception: { "
			  "\"What\": \"" << e.what() << "\""
			", " << request <<
			" }";
		cumulative_stats_[CONSUMER_PC_MMT_LOGIN_EXCEPTION]++;
	}

/* Not saving the returned handle as we will destroy the /consumer/ to logout,
 * reference:
 * 7.4.10.6 Other Cleanup
 * Note: The application may call destroy() on an Event Source without having
 * closed all Event Streams. RFA will internally unregister all open Event
 * Streams in this case.
 */
	VLOG(3) << "Registering OMM item interest for MMT_LOGIN.";
	rfa::sessionLayer::OMMItemIntSpec ommItemIntSpec;
	ommItemIntSpec.setMsg (&request);
	item_handle_.reset (omm_consumer_->registerClient (event_queue_.get(), &ommItemIntSpec, *this, nullptr /* closure */),
				[this](rfa::common::Handle* handle) { omm_consumer_->unregisterClient (handle); });
	cumulative_stats_[CONSUMER_PC_MMT_LOGIN_SENT]++;
	if (!(bool)item_handle_)
		return false;

/* Store negotiated Reuters Wire Format version information. */
	rfa::data::Map map;
	map.setAssociatedMetaInfo (*item_handle_.get());
	rwf_major_version_ = map.getMajorVersion();
	rwf_minor_version_ = map.getMinorVersion();
	LOG(INFO) << "RWF: { "
		     "\"MajorVersion\": " << (unsigned)rwf_major_version_ <<
		   ", \"MinorVersion\": " << (unsigned)rwf_minor_version_ <<
		   " }";
	return true;
}

bool
torikuru::consumer_t::SendItemRequest (
	std::shared_ptr<item_stream_t> item_stream	
	)
throw (rfa::common::InvalidUsageException)
{
	VLOG(2) << "Sending market price request.";
	rfa::message::ReqMsg request;

	request.setMsgModelType (rfa::rdm::MMT_MARKET_PRICE);
/* we don't care about the initial image but the API will complain if not requested.
 *
 * InvalidUsageException: {
 *   "StatusText": "InteractionType without expected 'ReqMsg::InitialImageFlag' is Invalid."
 * }
 */
	request.setInteractionType (rfa::message::ReqMsg::InitialImageFlag | rfa::message::ReqMsg::InterestAfterRefreshFlag);

	rfa::message::AttribInfo attribInfo;
	attribInfo.setNameType (rfa::rdm::INSTRUMENT_NAME_RIC);
	attribInfo.setName (item_stream->rfa_item_name);
	attribInfo.setServiceName (item_stream->rfa_service_name);

	request.setAttribInfo (attribInfo);

	if (DCHECK_IS_ON()) {
/* 4.2.8 Message Validation.  RFA provides an interface to verify that
 * constructed messages of these types conform to the Reuters Domain
 * Models as specified in RFA API 7 RDM Usage Guide.
 */
		uint8_t validation_status = rfa::message::MsgValidationError;
		try {
			RFA_String warningText;
			validation_status = request.validateMsg (&warningText);
			if (rfa::message::MsgValidationWarning == validation_status)
				LOG(WARNING) << "validateMsg: { \"warningText\": \"" << warningText << "\" }";
			cumulative_stats_[CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_VALIDATED]++;
		} catch (const rfa::common::InvalidUsageException& e) {
			LOG(ERROR) << "InvalidUsageException: { " <<
					   "\"StatusText\": \"" << e.getStatus().getStatusText() << "\""
					", " << request <<
				      " }";
			cumulative_stats_[CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_MALFORMED]++;
		} catch (const std::exception& e) {
			LOG(ERROR) << "Rfa::Exception: { "
				  "\"What\": \"" << e.what() << "\""
				", " << request <<
				" }";
			cumulative_stats_[CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_EXCEPTION]++;
		}
	}

	VLOG(3) << "Registering OMM item interest for MMT_MARKET_PRICE.";
	rfa::sessionLayer::OMMItemIntSpec ommItemIntSpec;
	ommItemIntSpec.setMsg (&request);
	item_stream->item_handle = omm_consumer_->registerClient (event_queue_.get(), &ommItemIntSpec, *this, item_stream.get() /* closure */);
	cumulative_stats_[CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_SENT]++;
	if (nullptr == item_stream->item_handle)
		return false;
	return true;
}

bool
torikuru::consumer_t::AddSubscription (
	std::shared_ptr<item_stream_t> item_stream	
	)
throw (rfa::common::InvalidUsageException)
{
	VLOG(2) << "Adding market data subscription.";

	rfa::sessionLayer::MarketDataItemSub marketDataItemSub;
	marketDataItemSub.setServiceName (item_stream->rfa_service_name);
	marketDataItemSub.setItemName (item_stream->rfa_item_name);
	item_stream->item_handle = market_data_subscriber_->subscribe (*event_queue_.get(), marketDataItemSub, *this, item_stream.get() /* closure */);
	if (nullptr == item_stream->item_handle)
		return false;
	return true;
}

/* Re-register entire directory for new handles.
 */
bool
torikuru::consumer_t::Resubscribe()
{
	if (LowerCaseEqualsASCII (config_.protocol, connections::kRSSL))
	{
		if (!(bool)omm_consumer_) {
			LOG(WARNING) << "Resubscribe whilst consumer is invalid.";
			return false;
		}

		for (auto& it : directory_)
			if (auto sp = it.second.lock())
/* only non-fulfilled items */
				if (nullptr == sp->item_handle)
					SendItemRequest (sp);

		return true;
	}
	else if (LowerCaseEqualsASCII (config_.protocol, connections::kSSLED))
	{
		if (!(bool)market_data_subscriber_) {
			LOG(WARNING) << "Resubscribe whilst consumer is invalid.";
			return false;
		}

		for (auto& it : directory_)
			if (auto sp = it.second.lock())
/* only non-fulfilled items */
				if (nullptr == sp->item_handle)
					AddSubscription (sp);
		return true;
	}
	
	LOG(ERROR) << "Unsupported transport protocol \"" << config_.protocol << "\".";
	return false;
}

/* Create an item stream for a given symbol name.  The Item Stream maintains
 * the provider state on behalf of the application.
 */
bool
torikuru::consumer_t::CreateItemStream (
	const char* item_name,
	std::shared_ptr<item_stream_t> item_stream
	)
throw (rfa::common::InvalidUsageException)
{
	VLOG(4) << "Creating item stream for RIC \"" << item_name << "\" on service \"" << config_.service_name << "\".";
	item_stream->rfa_item_name.set (item_name, 0, true);
	item_stream->rfa_service_name.set (config_.service_name.c_str(), 0, true);
	if (!is_muted_) {
		if (!SendItemRequest (item_stream))
			return false;
	} else {
/* no-op */
	}
	const std::string key (item_name);
	auto status = directory_.emplace (std::make_pair (key, item_stream));
	if (!status.second)
		return false;
	CHECK (true == status.second);
	CHECK (directory_.end() != directory_.find (key));
	DVLOG(4) << "Directory size: " << directory_.size();
	last_activity_ = boost::posix_time::microsec_clock::universal_time();
	return true;
}

void
torikuru::consumer_t::processEvent (
	const rfa::common::Event& event_
	)
{
	VLOG(1) << event_;
	cumulative_stats_[CONSUMER_PC_RFA_EVENTS_RECEIVED]++;
	switch (event_.getType()) {
	case rfa::sessionLayer::OMMItemEventEnum:
		OnOMMItemEvent (static_cast<const rfa::sessionLayer::OMMItemEvent&>(event_));
		break;

        case rfa::sessionLayer::OMMCmdErrorEventEnum:
                OnOMMCmdErrorEvent (static_cast<const rfa::sessionLayer::OMMCmdErrorEvent&>(event_));
                break;

	case rfa::sessionLayer::MarketDataSvcEventEnum:
		OnMarketDataSvcEvent (static_cast<const rfa::sessionLayer::MarketDataSvcEvent&>(event_));
		break;

	case rfa::sessionLayer::ConnectionEventEnum:
		OnConnectionEvent (static_cast<const rfa::sessionLayer::ConnectionEvent&>(event_));
		break;

	case rfa::sessionLayer::EntitlementsAuthenticationEventEnum:
		OnEntitlementsAuthenticationEvent (static_cast<const rfa::sessionLayer::EntitlementsAuthenticationEvent&>(event_));
		break;

	case rfa::sessionLayer::MarketDataItemEventEnum:
		OnMarketDataItemEvent (static_cast<const rfa::sessionLayer::MarketDataItemEvent&>(event_));
		break;

/* Licensing was removed in the RFA 5.0.1 C++ Edition release. */
        default:
		cumulative_stats_[CONSUMER_PC_RFA_EVENTS_DISCARDED]++;
		LOG(WARNING) << "Uncaught: " << event_;
                break;
        }
}

/* 7.5.8.1 Handling Item Events (Login Events).
 */
void
torikuru::consumer_t::OnOMMItemEvent (
	const rfa::sessionLayer::OMMItemEvent&	item_event
	)
{
	cumulative_stats_[CONSUMER_PC_OMM_ITEM_EVENTS_RECEIVED]++;
	const rfa::common::Msg& msg = item_event.getMsg();

/* Verify event is a response event */
	if (rfa::message::RespMsgEnum != msg.getMsgType()) {
		cumulative_stats_[CONSUMER_PC_OMM_ITEM_EVENTS_DISCARDED]++;
		LOG(WARNING) << "Uncaught: " << msg;
		return;
	}

	OnRespMsg (static_cast<const rfa::message::RespMsg&>(msg), item_event.getClosure());
}

void
torikuru::consumer_t::OnRespMsg (
	const rfa::message::RespMsg&	reply_msg,
	void* closure
	)
{
	cumulative_stats_[CONSUMER_PC_RESPONSE_MSGS_RECEIVED]++;
	switch (reply_msg.getMsgModelType()) {
	case rfa::rdm::MMT_LOGIN:
		OnLoginResponse (reply_msg);
		break;

	case rfa::rdm::MMT_MARKET_PRICE:
		OnMarketPrice (reply_msg, closure);
		break;

	default:
		cumulative_stats_[CONSUMER_PC_RESPONSE_MSGS_DISCARDED]++;
		LOG(WARNING) << "Uncaught: " << reply_msg;
		break;
	}
}

void
torikuru::consumer_t::OnLoginResponse (
	const rfa::message::RespMsg&	reply_msg
	)
{
	cumulative_stats_[CONSUMER_PC_MMT_LOGIN_RESPONSE_RECEIVED]++;
	const rfa::common::RespStatus& respStatus = reply_msg.getRespStatus();

/* save state */
	stream_state_ = respStatus.getStreamState();
	data_state_   = respStatus.getDataState();

	switch (stream_state_) {
	case rfa::common::RespStatus::OpenEnum:
		switch (data_state_) {
		case rfa::common::RespStatus::OkEnum:
			OnLoginSuccess (reply_msg);
			break;

		case rfa::common::RespStatus::SuspectEnum:
			OnLoginSuspect (reply_msg);
			break;

		default:
			cumulative_stats_[CONSUMER_PC_MMT_LOGIN_RESPONSE_DISCARDED]++;
			LOG(WARNING) << "Uncaught: " << reply_msg;
			break;
		}
		break;

	case rfa::common::RespStatus::ClosedEnum:
		OnLoginClosed (reply_msg);
		break;

	default:
		cumulative_stats_[CONSUMER_PC_MMT_LOGIN_RESPONSE_DISCARDED]++;
		LOG(WARNING) << "Uncaught: " << reply_msg;
		break;
	}
}

void
torikuru::consumer_t::OnMarketPrice (
	const rfa::message::RespMsg&	reply_msg,
	void* closure
	)
{
	cumulative_stats_[CONSUMER_PC_MMT_MARKET_PRICE_RECEIVED]++;
	item_stream_t* item_stream = reinterpret_cast<item_stream_t*> (closure);
	CHECK (nullptr != item_stream);

	const auto now (boost::posix_time::second_clock::universal_time());
	item_stream->last_activity = now;
	item_stream->msg_count++;

	switch (reply_msg.getRespType()) {
	case rfa::message::RespMsg::RefreshEnum:
		item_stream->last_refresh = now;
		item_stream->refresh_received++;
		break;
	case rfa::message::RespMsg::StatusEnum:
		item_stream->last_status = now;
		item_stream->status_received++;
		break;
	case rfa::message::RespMsg::UpdateEnum:
		item_stream->last_update = now;
		item_stream->update_received++;
		break;
	default: break;
	}
}

/* 7.5.8.1.1 Login Success.
 * The stream state is OpenEnum one has received login permission from the
 * back-end infrastructure and the non-interactive provider can start to
 * publish data, including the service directory, dictionary, and other
 * response messages of different message model types.
 */
void
torikuru::consumer_t::OnLoginSuccess (
	const rfa::message::RespMsg&			login_msg
	)
{
	cumulative_stats_[CONSUMER_PC_MMT_LOGIN_SUCCESS]++;
	try {
/* SendDirectoryRequest(); */
		Resubscribe();
		LOG(INFO) << "Unmuting consumer.";
		is_muted_ = false;

/* ignore any error */
	} catch (const rfa::common::InvalidUsageException& e) {
		LOG(ERROR) << "MMT_DIRECTORY::InvalidUsageException: { StatusText: \"" << e.getStatus().getStatusText() << "\" }";
/* cannot publish until directory is sent. */
	} catch (const std::exception& e) {
		LOG(ERROR) << "Rfa::Exception: { "
			"\"What\": \"" << e.what() << "\" }";
	}
}

/* 7.5.8.1.2 Other Login States.
 * All connections are down. The application should stop publishing; it may
 * resume once the data state becomes OkEnum.
 */
void
torikuru::consumer_t::OnLoginSuspect (
	const rfa::message::RespMsg&			suspect_msg
	)
{
	cumulative_stats_[CONSUMER_PC_MMT_LOGIN_SUSPECT]++;
	is_muted_ = true;
}

/* 7.5.8.1.2 Other Login States.
 * The login failed, and the provider application failed to get permission
 * from the back-end infrastructure. In this case, the provider application
 * cannot start to publish data.
 */
void
torikuru::consumer_t::OnLoginClosed (
	const rfa::message::RespMsg&			logout_msg
	)
{
	cumulative_stats_[CONSUMER_PC_MMT_LOGIN_CLOSED]++;
	is_muted_ = true;
}

/* 7.5.8.2 Handling CmdError Events.
 * Represents an error Event that is generated during the submit() call on the
 * OMM non-interactive provider. This Event gives the provider application
 * access to the Cmd, CmdID, closure and OMMErrorStatus for the Cmd that
 * failed.
 */
void
torikuru::consumer_t::OnOMMCmdErrorEvent (
	const rfa::sessionLayer::OMMCmdErrorEvent& error
	)
{
	cumulative_stats_[CONSUMER_PC_OMM_CMD_ERRORS]++;
	LOG(ERROR) << "OMMCmdErrorEvent: { "
		  "\"CmdId\": " << error.getCmdID() <<
		", \"State\": " << error.getStatus().getState() <<
		", \"StatusCode\": " << error.getStatus().getStatusCode() <<
		", \"StatusText\": \"" << error.getStatus().getStatusText() << "\" }";
}


void
torikuru::consumer_t::OnMarketDataSvcEvent (
	const rfa::sessionLayer::MarketDataSvcEvent& event
	)
{
	cumulative_stats_[CONSUMER_PC_MARKET_DATA_SVC_EVENTS_RECEIVED]++;
}

void
torikuru::consumer_t::OnConnectionEvent (
	const rfa::sessionLayer::ConnectionEvent& event
	)
{
	cumulative_stats_[CONSUMER_PC_CONNECTION_EVENTS_RECEIVED]++;
}

void
torikuru::consumer_t::OnEntitlementsAuthenticationEvent (
	const rfa::sessionLayer::EntitlementsAuthenticationEvent& event
	)
{
	cumulative_stats_[CONSUMER_PC_ENTITLEMENT_EVENTS_RECEIVED]++;
}

void
torikuru::consumer_t::OnMarketDataItemEvent (
	const rfa::sessionLayer::MarketDataItemEvent&	item_event
	)
{
	timeval tv;
	gettimeofday (&tv, nullptr);

	cumulative_stats_[CONSUMER_PC_MARKET_DATA_ITEM_EVENTS_RECEIVED]++;
	item_stream_t* item_stream = reinterpret_cast<item_stream_t*> (item_event.getClosure());
	CHECK (nullptr != item_stream);

/* Sanity check on stream state */
	if (item_event.isEventStreamClosed()) {
		VLOG(2) << "Stream closed for \"" << item_event.getItemName().c_str() << "\".";
		if (!item_stream->is_closed) {
			refresh_count_++;
			item_stream->is_closed = true;
		}
	}

//	struct tm local_time = {0};
//	localtime_r (&tv.tv_sec, &local_time);
//	struct tm* tm_time = &local_time;

	switch (item_event.getMarketDataMsgType()) {
	case rfa::sessionLayer::MarketDataItemEvent::Image:
		if (0 == item_stream->refresh_received++) {
			refresh_count_++;
			if (disable_refresh_)
				goto check_sync;
			if (!interest_after_refresh_) {
				item_stream->is_closed = true;
				market_data_subscriber_->unregisterClient (*item_stream->item_handle);
			}
		}
		break;

	case rfa::sessionLayer::MarketDataItemEvent::Update:
		item_stream->update_received++;
		if (disable_update_)
			goto check_sync;
		break;

	case rfa::sessionLayer::MarketDataItemEvent::UnsolicitedImage:
		LOG(WARNING) << "Ignoring unsolicited image";
		goto check_sync;

	case rfa::sessionLayer::MarketDataItemEvent::Status:
		VLOG(1) << "Ignoring status";
		item_stream->status_received++;
		goto check_sync;

	case rfa::sessionLayer::MarketDataItemEvent::Correction:
		LOG(WARNING) << "Received correction";
		break;

	case rfa::sessionLayer::MarketDataItemEvent::ClosingRun:
		LOG(WARNING) << "Ignoring closing run";
		goto check_sync;

	case rfa::sessionLayer::MarketDataItemEvent::Rename:
		LOG(WARNING) << "Received rename";
		break;

	case rfa::sessionLayer::MarketDataItemEvent::PermissionData:
		LOG(WARNING) << "Ignoring permission data";
		goto check_sync;

	case rfa::sessionLayer::MarketDataItemEvent::GroupChange:
		LOG(WARNING) << "Ignoring group change";
		goto check_sync;

	default:
		LOG(WARNING) << "Unhandled market data message type (" << item_event.getMarketDataMsgType() << ")";
		break;
	}

	if (rfa::sessionLayer::MarketDataEnums::Marketfeed != item_event.getDataFormat()) {
		std::ostringstream format;
		switch (item_event.getDataFormat()) {
		case rfa::sessionLayer::MarketDataEnums::Unknown:
			format << "Unknown";
			break;
		case rfa::sessionLayer::MarketDataEnums::ANSI_Page:
			format << "ANSI_Page";
			break;
		case rfa::sessionLayer::MarketDataEnums::Marketfeed:
			format << "Marketfeed";
			break;
		case rfa::sessionLayer::MarketDataEnums::QForm:
			format << "QForm";
			break;
		case rfa::sessionLayer::MarketDataEnums::TibMsgSelfDescribed:
			format << "TibMsg";
			break;
		default:
			format << item_event.getDataFormat();
			break;
		}
		
		LOG(WARNING) << "Unsupported data format (" << format.str() << ") in market data item event.";
		market_data_subscriber_->unregisterClient (*item_stream->item_handle);
		goto check_sync;
	}

	mfeed_.Clear();

/* meta-data */
	mfeed_.set_tv_sec (tv.tv_sec);
	mfeed_.set_tv_usec (tv.tv_usec);
	mfeed_.set_message_type (item_event.getMarketDataMsgType());
	mfeed_.set_service_name (item_event.getServiceName().data(), item_event.getServiceName().size());
	mfeed_.set_item_name (item_event.getItemName().data(), item_event.getItemName().size());
	if (!item_event.getNewItemName().empty())
		mfeed_.set_new_item_name (item_event.getNewItemName().data(), item_event.getNewItemName().size());

/* payload */
	if (!item_event.getBuffer().isEmpty())
		mfeed_.set_packed_buffer (item_event.getBuffer().c_buf(), item_event.getBuffer().size());

	if (nullptr != coded_stream_) {
		if (!mfeed_.IsInitialized()) {
			LOG(ERROR) << "Ignoring message: " << mfeed_.InitializationErrorString();
		} else {
			const uint32_t size = mfeed_.ByteSize();
			coded_stream_->WriteVarint32 (size);
			mfeed_.SerializeWithCachedSizes (coded_stream_);
		}
	}

check_sync:
/* Refresh state check */
	if (!in_sync_ && refresh_count_ == directory_.size()) {
		in_sync_ = true;
		LOG(INFO) << "Service "  << item_event.getServiceName().c_str() << " synchronised.";
		if ((bool)on_sync_)
			on_sync_();
	}
}

#if 0
	TibMsg msg;
	TibField field;
	TibErr err;

	err = msg.UnPack (reinterpret_cast<char*> (const_cast<unsigned char*> (buffer.c_buf())), buffer.size());
/* (TIBMSG_OK != err) raises compilation error in mismatching types */
	if (err != TIBMSG_OK) {
		LOG(WARNING) << "TibMsg::UnPack failed on market data event.";
		return;
	}

	char buf[256];
	std::ostringstream oss;
	oss << std::setfill('0')
            << std::setw(4) << 1900 + tm_time->tm_year
	    << '-'
            << std::setw(2) << 1 + tm_time->tm_mon
	    << '-'
            << std::setw(2) << tm_time->tm_mday
            << 'T'
            << std::setw(2) << tm_time->tm_hour
            << ':'
            << std::setw(2) << tm_time->tm_min
            << ':'
            << std::setw(2) << tm_time->tm_sec
            << '.'
	    << std::setw(6) << tv.tv_usec
            << ','
	    << item_event.getServiceName()
	    << ','
	    << item_event.getItemName();
	for (field.First (&msg); field.status == TIBMSG_OK; field.Next()) {
		memset (buf, 0, sizeof (buf));
		err = field.Convert (buf, sizeof (buf));
		if (err == TIBMSG_OK) {
			const char* p = buf;
			oss << ',';
			while (*p) {
				if (std::isprint (*p))
					oss << *p;
				else
					oss << "\\x" << std::hex << std::setfill ('0') << std::setw (2) << (static_cast<unsigned> (*p) & 0xff);
				++p;
			}
		} else {
			LOG(WARNING) << "Failed to convert field \"" << field.Name() << "\".";
		}
	}
	LOG(INFO) << oss.str();
}
#endif

/* eof */
