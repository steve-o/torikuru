/* RFA consumer.
 */

#ifndef __CONSUMER_HH__
#define __CONSUMER_HH__
#pragma once

#include <cstdint>
#include <functional>

/* Boost Chrono. */
#include <boost/chrono.hpp>

/* Boost Posix Time */
#include <boost/date_time/posix_time/posix_time.hpp>

/* Boost unordered map: bypass 2^19 limit in MSVC std::unordered_map */
#include <boost/unordered_map.hpp>

/* Boost noncopyable base class */
#include <boost/utility.hpp>

/* Boost threading. */
#include <boost/thread.hpp>

/* RFA 7.2 */
#include <rfa/rfa.hh>

/* Protocol Buffers */
#include <google/protobuf/io/gzip_stream.h>

#include "rfa.hh"
#include "config.hh"
#include "deleter.hh"

#include <archive.pb.h>

namespace torikuru
{
/* Performance Counters */
	enum {
		CONSUMER_PC_RFA_EVENTS_RECEIVED,
		CONSUMER_PC_RFA_EVENTS_DISCARDED,
		CONSUMER_PC_OMM_ITEM_EVENTS_RECEIVED,
		CONSUMER_PC_OMM_ITEM_EVENTS_DISCARDED,
		CONSUMER_PC_RESPONSE_MSGS_RECEIVED,
		CONSUMER_PC_RESPONSE_MSGS_DISCARDED,
		CONSUMER_PC_MMT_LOGIN_RESPONSE_RECEIVED,
		CONSUMER_PC_MMT_LOGIN_RESPONSE_DISCARDED,
		CONSUMER_PC_MMT_LOGIN_SUCCESS,
		CONSUMER_PC_MMT_LOGIN_SUSPECT,
		CONSUMER_PC_MMT_LOGIN_CLOSED,
		CONSUMER_PC_OMM_CMD_ERRORS,
		CONSUMER_PC_MMT_LOGIN_VALIDATED,
		CONSUMER_PC_MMT_LOGIN_MALFORMED,
		CONSUMER_PC_MMT_LOGIN_EXCEPTION,
		CONSUMER_PC_MMT_LOGIN_SENT,
		CONSUMER_PC_MMT_MARKET_PRICE_RECEIVED,
		CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_VALIDATED,
		CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_MALFORMED,
		CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_EXCEPTION,
		CONSUMER_PC_MMT_MARKET_PRICE_REQUEST_SENT,
		CONSUMER_PC_MARKET_DATA_SVC_EVENTS_RECEIVED,
		CONSUMER_PC_CONNECTION_EVENTS_RECEIVED,
		CONSUMER_PC_ENTITLEMENT_EVENTS_RECEIVED,
		CONSUMER_PC_LICENSE_EVENTS_RECEIVED,
		CONSUMER_PC_MARKET_DATA_ITEM_EVENTS_RECEIVED,
/* marker */
		CONSUMER_PC_MAX
	};

	class item_stream_t : boost::noncopyable
	{
	public:
		item_stream_t()
			: item_handle (nullptr),
			  msg_count (0),
			  last_activity (boost::posix_time::second_clock::universal_time()),
			  refresh_received (0),
			  status_received (0),
			  update_received (0),
			  is_closed (false)
		{
		}

/* Fixed name for this stream. */
		rfa::common::RFA_String rfa_item_name;

/* Service origin, e.g. IDN_RDF */
		rfa::common::RFA_String rfa_service_name;

/* Subscription handle which is valid from login success to login close. */
		rfa::common::Handle* item_handle;

/* Performance counters */
		boost::posix_time::ptime last_activity;
		boost::posix_time::ptime last_refresh;
		boost::posix_time::ptime last_status;
		boost::posix_time::ptime last_update;
		uint32_t msg_count;		/* including unknown message types */
		uint32_t refresh_received;
		uint32_t status_received;
		uint32_t update_received;

		bool is_closed;
	};

	class session_t;

	class consumer_t :
		public rfa::common::Client,
		boost::noncopyable
	{
	public:
		consumer_t (const session_config_t& config, std::shared_ptr<rfa_t> rfa, std::shared_ptr<rfa::common::EventQueue> event_queue, google::protobuf::io::CodedOutputStream* coded_stream);
		~consumer_t();

		bool Init (bool disable_update, bool disable_refresh, bool interest_after_refresh, std::function<void()>& on_sync) throw (rfa::common::InvalidConfigurationException, rfa::common::InvalidUsageException);

		bool CreateItemStream (const char* name, std::shared_ptr<item_stream_t> item_stream) throw (rfa::common::InvalidUsageException);
		bool Resubscribe();

/* RFA event callback. */
		void processEvent (const rfa::common::Event& event) override;

		uint8_t GetRwfMajorVersion() const {
			return rwf_major_version_;
		}
		uint8_t GetRwfMinorVersion() const {
			return rwf_minor_version_;
		}

	private:
		void OnOMMItemEvent (const rfa::sessionLayer::OMMItemEvent& event);
                void OnRespMsg (const rfa::message::RespMsg& msg, void* closure);
                void OnLoginResponse (const rfa::message::RespMsg& msg);
                void OnLoginSuccess (const rfa::message::RespMsg& msg);
                void OnLoginSuspect (const rfa::message::RespMsg& msg);
                void OnLoginClosed (const rfa::message::RespMsg& msg);
                void OnMarketPrice (const rfa::message::RespMsg& msg, void* closure);
		void OnOMMCmdErrorEvent (const rfa::sessionLayer::OMMCmdErrorEvent& event);

		void OnMarketDataSvcEvent (const rfa::sessionLayer::MarketDataSvcEvent &event);
		void OnConnectionEvent (const rfa::sessionLayer::ConnectionEvent &event);
		void OnEntitlementsAuthenticationEvent (const rfa::sessionLayer::EntitlementsAuthenticationEvent &event);
		void OnMarketDataItemEvent (const rfa::sessionLayer::MarketDataItemEvent &Event);

		bool SendLoginRequest() throw (rfa::common::InvalidUsageException);
		bool SendItemRequest (std::shared_ptr<item_stream_t> item_stream) throw (rfa::common::InvalidUsageException);
		bool AddSubscription (std::shared_ptr<item_stream_t> item_stream) throw (rfa::common::InvalidUsageException);

		const session_config_t& config_;

/* RFA context. */
		std::shared_ptr<rfa_t> rfa_;

/* RFA asynchronous event queue. */
		std::shared_ptr<rfa::common::EventQueue> event_queue_;

/* RFA session defines one or more connections for horizontal scaling. */
		std::unique_ptr<rfa::sessionLayer::Session, internal::release_deleter> session_;

/* RFA OMM consumer interface. */
		std::unique_ptr<rfa::sessionLayer::OMMConsumer, internal::destroy_deleter> omm_consumer_;

/* RFA market data subscriber interface. */
		std::unique_ptr<rfa::sessionLayer::MarketDataSubscriber, internal::destroy_deleter> market_data_subscriber_;

/* RFA Error Item event consumer */
		rfa::common::Handle* error_item_handle_;
/* RFA Item event consumer */
		rfa::common::Handle* item_handle_;

		archive::Marketfeed mfeed_;
		google::protobuf::io::CodedOutputStream* coded_stream_;

		bool disable_update_;
		bool disable_refresh_;
		bool interest_after_refresh_;

		unsigned refresh_count_;
		bool in_sync_;

		std::function<void()> on_sync_;

/* Reuters Wire Format versions. */
		uint8_t rwf_major_version_;
		uint8_t rwf_minor_version_;

/* RFA will return a CmdError message if the provider application submits data
 * before receiving a login success message.  Mute downstream publishing until
 * permission is granted to submit data.
 */
		bool is_muted_;

/* Last RespStatus details. */
		int stream_state_;
		int data_state_;

/* Container of all item streams keyed by symbol name. */
		boost::unordered_map<std::string, std::weak_ptr<item_stream_t>> directory_;
		boost::shared_mutex directory_lock_;

/** Performance Counters **/
		boost::posix_time::ptime last_activity_;
		uint32_t cumulative_stats_[CONSUMER_PC_MAX];
		uint32_t snap_stats_[CONSUMER_PC_MAX];
	};

} /* namespace torikuru */

#endif /* __CONSUMER_HH__ */

/* eof */
