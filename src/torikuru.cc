/* RDM subscriber application.
 */

#include "torikuru.hh"

#define __STDC_FORMAT_MACROS
#include <cstdint>
#include <inttypes.h>
#include <functional>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "chromium/command_line.hh"
#include "chromium/file_util.hh"
#include "chromium/logging.hh"
#include "chromium/string_piece.hh"
#include "chromium/string_split.hh"
#include "chromium/string_util.hh"
#include "googleurl/url_parse.h"
#include "error.hh"
#include "rfa_logging.hh"
#include "rfaostream.hh"

/* RDM Usage Guide: Section 6.5: Enterprise Platform
 * For future compatibility, the DictionaryId should be set to 1 by providers.
 * The DictionaryId for the RDMFieldDictionary is 1.
 */
static const int kDictionaryId = 1;

/* RDM: Absolutely no idea. */
static const int kFieldListId = 3;


namespace switches {

//  Execution time limit in seconds.
const char kTimeLimit[]                     = "time-limit";

//  Consumer RFA session declaration.
const char kSession[]                       = "session";

//  Set of symbols to capture.
const char kSymbolPath[]		    = "symbol-path";

//  Output file for recording.
const char kOutputPath[]                    = "output-path";

//  Input file for unpacking.
const char kInputPath[]			    = "input-path";

//  Retrieve initial image only.
const char kDisableUpdate[]		    = "disable-update";

//  Record updates only.
const char kDisableRefresh[]		    = "disable-refresh";

//  Finish capture when all symbols return a refresh or status close.
const char kTerminateOnSync[]		    = "terminate-on-sync";

}  // namespace switches

std::list<torikuru::torikuru_t*> torikuru::torikuru_t::global_list_;
boost::shared_mutex torikuru::torikuru_t::global_list_lock_;

static std::weak_ptr<rfa::common::EventQueue> g_event_queue;


using rfa::common::RFA_String;

torikuru::torikuru_t::torikuru_t() :
	consumers_in_sync_ (0),
	output_fd_ (-1),
	output_stream_ (nullptr),
	gzip_stream_ (nullptr),
	coded_stream_ (nullptr)
{
	boost::unique_lock<boost::shared_mutex> (global_list_lock_);
	global_list_.push_back (this);
}

torikuru::torikuru_t::~torikuru_t()
{
/* Remove from list before clearing. */
	boost::unique_lock<boost::shared_mutex> (global_list_lock_);
	global_list_.remove (this);

	Clear();
	LOG(INFO) << "fin.";
}

bool
torikuru::torikuru_t::Init ()
{
	try {
/* Configuration. */
		CommandLine* command_line = CommandLine::ForCurrentProcess();

		std::string session = command_line->GetSwitchValueASCII (switches::kSession);
		if (!session.empty()) {
			std::vector<std::string> sessions;
			url_parse::Parsed parsed;
			url_parse::Component file_name;
			chromium::SplitString (session, ',', &sessions);
			for (const auto& url : sessions) {
				session_config_t session_config;
				VLOG(1) << "session: " << url;
/* Pass through Google URL http://code.google.com/p/google-url/
 */
				url_parse::ParseStandardURL (url.c_str(), static_cast<int>(url.size()), &parsed);
				if (parsed.scheme.is_valid()) {
					session_config.protocol.assign (url.c_str() + parsed.scheme.begin, parsed.scheme.len);
					VLOG(2) << "protocol: " << session_config.protocol;
				}
				if (parsed.username.is_valid()) {
					session_config.user_name.assign (url.c_str() + parsed.username.begin, parsed.username.len);
					VLOG(2) << "username: " << session_config.user_name;
				} else {
					session_config.user_name.assign (getenv ("LOGNAME"));
					VLOG(2) << "username: " << session_config.user_name << " (default)";
				}
				if (parsed.host.is_valid()) {
					const std::string server (url.c_str() + parsed.host.begin, parsed.host.len);
					session_config.servers.emplace_back (server);
					VLOG(2) << "host: " << server;
				} else {
					session_config.servers.emplace_back ("localhost");
					VLOG(2) << "host: localhost (default)";
				}
				if (parsed.port.is_valid()) {
					session_config.default_port.assign (url.c_str() + parsed.port.begin, parsed.port.len);
					VLOG(2) << "port: " << session_config.default_port;
				} else {
					VLOG(2) << "port: (default)";
				}
				if (parsed.path.is_valid()) {
					url_parse::ExtractFileName (url.c_str(), parsed.path, &file_name);
					if (file_name.is_valid()) {
						session_config.service_name.assign (url.c_str() + file_name.begin, file_name.len);
						VLOG(2) << "service: " << session_config.service_name;
					}
				}
				if (parsed.query.is_valid()) {
					url_parse::Component query = parsed.query;
					url_parse::Component key_range, value_range;
/* For each key-value pair, i.e. ?a=x&b=y&c=z -> (a,x) (b,y) (c,z)
 */
					while (url_parse::ExtractQueryKeyValue (url.c_str(), &query, &key_range, &value_range))
					{
/* Lazy std::string conversion for key
 */
						const chromium::StringPiece key (url.c_str() + key_range.begin, key_range.len);
/* Value must convert to add NULL terminator for conversion APIs.
 */
						if (key == "application-id") {
							session_config.application_id.assign (url.c_str() + value_range.begin, value_range.len);
							VLOG(2) << "application-id: " << session_config.application_id;
						} else if (key == "instance-id") {
							session_config.instance_id.assign (url.c_str() + value_range.begin, value_range.len);
							VLOG(2) << "instance-id: " << session_config.instance_id;
						} else if (key == "position") {
							session_config.position.assign (url.c_str() + value_range.begin, value_range.len);
							VLOG(2) << "position: " << session_config.position;
						}
					}
				}
/* Boiler plate naming */
				session_config.session_name.assign ("Session");
				session_config.session_name.append (session_config.service_name);
				session_config.connection_name.assign ("Connection");
				session_config.connection_name.append (session_config.service_name);
				session_config.consumer_name.assign ("Consumer");
				session_config.consumer_name.append (session_config.service_name);
				config_.sessions.emplace_back (session_config);
			}
		}

/* Symbol list */
		if (command_line->HasSwitch (switches::kSymbolPath)) {
			config_.symbol_path = command_line->GetSwitchValueASCII (switches::kSymbolPath);
			if (chromium::PathExists (config_.symbol_path)) {
				std::string contents;
				file_util::ReadFileToString (config_.symbol_path, &contents);
				chromium::SplitStringAlongWhitespace (contents, &config_.instruments);
			}
		}

/* Image handling */
		if (command_line->HasSwitch (switches::kDisableUpdate))
			config_.disable_update = true;
		if (command_line->HasSwitch (switches::kDisableRefresh))
			config_.disable_refresh = true;
		if (command_line->HasSwitch (switches::kTerminateOnSync))
			config_.terminate_on_sync = true;
/* Output stream */
		if (command_line->HasSwitch (switches::kOutputPath))
			config_.output_path = command_line->GetSwitchValueASCII (switches::kOutputPath);
/* Input stream */
		if (command_line->HasSwitch (switches::kInputPath))
			config_.input_path = command_line->GetSwitchValueASCII (switches::kInputPath);
/* Run-time limit */
		if (command_line->HasSwitch (switches::kTimeLimit))
			config_.time_limit = command_line->GetSwitchValueASCII (switches::kTimeLimit);

		LOG(INFO) << config_;

/* RFA context. */
		rfa_.reset (new rfa_t (config_));
		if (!(bool)rfa_ || !rfa_->Init())
			return false;

/* RFA asynchronous event queue. */
		const RFA_String eventQueueName (config_.event_queue_name.c_str(), 0, false);
		event_queue_.reset (rfa::common::EventQueue::create (eventQueueName), std::mem_fun (&rfa::common::EventQueue::destroy));
		if (!(bool)event_queue_)
			return false;
/* Create weak pointer to handle application shutdown. */
		g_event_queue = event_queue_;

/* RFA logging. */
		log_.reset (new logging::rfa::LogEventProvider (config_, event_queue_));
		if (!(bool)log_ || !log_->Register())
			return false;

		if (config_.input_path.empty())
		{
/* Archive stream */
			if (!config_.output_path.empty()) {
				LOG(INFO) << "Appending to output file \"" << config_.output_path << "\".";
				output_fd_ = open (config_.output_path.c_str(),
						O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE,
						S_IREAD | S_IWRITE);
				if (-1 == output_fd_) {
					LOG(ERROR) << "Failed to open file \"" << config_.output_path << "\".";
					return false;
				}
				output_stream_ = new google::protobuf::io::FileOutputStream (output_fd_);
				google::protobuf::io::GzipOutputStream::Options options;
				options.format = google::protobuf::io::GzipOutputStream::ZLIB;
				options.compression_level = 1;	/* best speed */
				gzip_stream_ = new google::protobuf::io::GzipOutputStream (output_stream_, options);
				coded_stream_ = new google::protobuf::io::CodedOutputStream (gzip_stream_);
//				coded_stream_ = new google::protobuf::io::CodedOutputStream (output_stream_);
			}
/* Prepare for sync state */
			std::function<void()> f0 = [this] {
				if (++consumers_in_sync_ == consumers_.size()) {
					LOG(INFO) << "All sessions synchronised.";
					if (config_.terminate_on_sync) {
						LOG(INFO) << "Terminating capture session.";
						event_queue_->deactivate();
					}
				}
			};

/* RFA consumer. */
			for (const auto& session_config : config_.sessions) {
				auto consumer = std::make_shared<consumer_t> (session_config, rfa_, event_queue_, coded_stream_);
				if (!(bool)consumer || !consumer->Init (config_.disable_update, config_.disable_refresh, !config_.terminate_on_sync, f0))
					return false;
				consumers_.emplace_back (consumer);
			}

/* Create state for subscribed RIC. */
			for (const auto& instrument : config_.instruments) {
				for (auto& consumer : consumers_) {
					auto stream = std::make_shared<subscription_stream_t> ();
					if (!(bool)stream)
						return false;
					if (consumer->CreateItemStream (instrument.c_str(), stream))
						streams_.emplace_back (stream);
					else
						LOG(WARNING) << "Cannot create stream for \"" << instrument << "\".";
				}
				VLOG(1) << instrument;
			}

/* Submit subscriptions */
			for (auto consumer : consumers_)
				consumer->Resubscribe();
		}

	} catch (const rfa::common::InvalidUsageException& e) {
		LOG(ERROR) << "InvalidUsageException: { "
			  "\"Severity\": \"" << internal::severity_string (e.getSeverity()) << "\""
			", \"Classification\": \"" << internal::classification_string (e.getClassification()) << "\""
			", \"StatusText\": \"" << e.getStatus().getStatusText() << "\" }";
		return false;
	} catch (const rfa::common::InvalidConfigurationException& e) {
		LOG(ERROR) << "InvalidConfigurationException: { "
			  "\"Severity\": \"" << internal::severity_string (e.getSeverity()) << "\""
			", \"Classification\": \"" << internal::classification_string (e.getClassification()) << "\""
			", \"StatusText\": \"" << e.getStatus().getStatusText() << "\""
			", \"ParameterName\": \"" << e.getParameterName() << "\""
			", \"ParameterValue\": \"" << e.getParameterValue() << "\" }";
		return false;
	} catch (const std::exception& e) {
		LOG(ERROR) << "Rfa::Exception: { "
			"\"What\": \"" << e.what() << "\" }";
		return false;
	}

	try {
	} catch (const std::exception& e) {
		LOG(ERROR) << "Exception: { "
			"\"What\": \"" << e.what() << "\" }";
		return false;
	}
	return true;
}

int
torikuru::torikuru_t::Run()
{
	if (!Init()) {
		LOG(INFO) << "Init failed, cleaning up.";
		Clear();
		return EXIT_FAILURE;
	}

	if (config_.input_path.empty()) {
		LOG(INFO) << "Init complete, entering main loop.";
		MainLoop();
		LOG(INFO) << "Main loop terminated.";
	} else {
		LOG(INFO) << "Init complete, procesing pre-recorded stream.";
		Convert();
		LOG(INFO) << "Processing complete.";
	}
	Clear();
	return EXIT_SUCCESS;
}

void
torikuru::torikuru_t::MainLoop()
{
	time_t now = time (nullptr);
	time_t start_time = now;
	time_t end_time = start_time + std::atoi (config_.time_limit.c_str());
	while (event_queue_->isActive() && (now < end_time || end_time == start_time)) {
		event_queue_->dispatch (100);
		now = time (nullptr);
	}

	if (end_time != start_time)
		LOG(INFO) << "Configured runtime elapsed, terminating ...";
}

void
torikuru::torikuru_t::Convert()
{
	LOG(INFO) << "Opening input file \"" << config_.input_path << "\".";
	int input_fd = open (config_.input_path.c_str(),
				O_RDONLY | O_LARGEFILE | O_NOATIME);
	if (-1 == input_fd) {
		LOG(ERROR) << "Failed to open file \"" << config_.input_path << "\".";
		return;
	}

	uint32_t size;
	archive::Marketfeed mfeed;
	TibMsg msg;
	TibField field;
	std::string name;

	std::unordered_map<std::string, std::unique_ptr<std::fstream>> service_map;
	for (const auto& session : config_.sessions) {
		std::vector<std::string> subst;
		subst.emplace_back (session.service_name);
		std::string filename = ReplaceStringPlaceholders (config_.output_path, subst, nullptr);
		LOG(INFO) << "Exporting service \"" << session.service_name << "\" as \"" << filename << "\".";
		std::unique_ptr<std::fstream> fs (new std::fstream (filename, std::ios::out | std::ios::trunc));
		service_map.emplace (session.service_name, std::move (fs));
	}

	std::unordered_set<std::string> symbol_set;
	if (!config_.instruments.empty()) {
		for (const auto& instrument : config_.instruments)
			symbol_set.emplace (instrument);
	}

	std::unordered_map<std::string, int> fid_map;
	fid_map.emplace (std::string ("service"), 0);
	fid_map.emplace (std::string ("symbol"), 0);
	fid_map.emplace (std::string ("time"), 0);
	fid_map.emplace (std::string ("type"), 0);

/* 1st pass - find unique FIDs */
	{
		lseek (input_fd, SEEK_SET, 0);
		google::protobuf::io::FileInputStream input_stream (input_fd);
		google::protobuf::io::GzipInputStream gzip_stream (&input_stream);
		std::unordered_set<std::string> fids;

		size = 0;
		while (true) {
			google::protobuf::io::CodedInputStream coded_stream (&gzip_stream);
			if (!coded_stream.ReadVarint32 (&size))
				break;
			const int limit = coded_stream.PushLimit (size);
			const bool is_valid = mfeed.ParseFromCodedStream (&coded_stream);
			coded_stream.PopLimit (limit);

/* filter on symbol name */
			if (mfeed.has_item_name() &&
			    !symbol_set.empty() &&
			    symbol_set.end() == symbol_set.find (mfeed.item_name()))
				continue;

			if (is_valid &&
			    msg.UnPack (const_cast<char*> (mfeed.packed_buffer().c_str()), mfeed.packed_buffer().size()) == TIBMSG_OK)
			{
				for (field.First (&msg); field.status == TIBMSG_OK; field.Next()) {
					name.assign (field.Name(), field.NameSize() == 0 ? 0 : strlen (field.Name()));
					if (fids.end() == fids.find (name))
						fids.emplace (name);
				}
			}
		}

		std::vector<std::string> columns (fids.size());
		std::copy (fids.begin(), fids.end(), columns.begin());
		int i = fid_map.size();
		for (const auto& column : columns)
			fid_map.emplace (column, i++);

		for (const auto& service : service_map)
			*service.second << "service,symbol,time,type," << JoinString (columns, ',') << std::endl;
		LOG(INFO) << fids.size() << " unique FIDs recorded.";
	}

/* 2nd pass - output CSVs */
	{
		lseek (input_fd, SEEK_SET, 0);
		google::protobuf::io::FileInputStream input_stream (input_fd);
		google::protobuf::io::GzipInputStream gzip_stream (&input_stream);
		char buf[256];
		timeval tv;
		struct tm local_time = {0};
		struct tm* tm_time = &local_time;
		unsigned i = 0;

		size = 0;
		while (true) {
			google::protobuf::io::CodedInputStream coded_stream (&gzip_stream);
			if (!coded_stream.ReadVarint32 (&size))
				break;
			const int limit = coded_stream.PushLimit (size);
			const bool is_valid = mfeed.ParseFromCodedStream (&coded_stream);
			coded_stream.PopLimit (limit);

			LOG_IF(WARNING, mfeed.packed_buffer().size() == 0);
			LOG_IF(WARNING, mfeed.packed_buffer().size() > 0xffff);

			if (mfeed.has_item_name() &&
			    !symbol_set.empty() &&
			    symbol_set.end() == symbol_set.find (mfeed.item_name()))
				continue;

			if (is_valid &&
			    msg.UnPack (const_cast<char*> (mfeed.packed_buffer().c_str()), mfeed.packed_buffer().size()) == TIBMSG_OK)
			{
				if (!mfeed.has_service_name()) {
					LOG(WARNING) << "service name is blank";
					continue;
				}
				if (!mfeed.has_item_name()) {
					LOG(WARNING) << "item name is blank";
					continue;
				}
				if (!mfeed.has_message_type()) {
					LOG(WARNING) << "message type is blank";
					continue;
				}
				std::vector<std::string> columns (fid_map.size());
				columns[0] = mfeed.service_name();
				columns[1] = mfeed.item_name();
				{
					std::ostringstream oss;
					tv.tv_sec = mfeed.has_tv_sec() ? mfeed.tv_sec() : 0;
					tv.tv_usec = mfeed.has_tv_usec() ? mfeed.tv_usec() : 0;
					localtime_r (&tv.tv_sec, &local_time);
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
				            << std::setw(6) << tv.tv_usec;
					columns[2] = oss.str();
				}
				{
					std::ostringstream oss;
					oss << mfeed.message_type();
					columns[3] = oss.str();
				}
				for (field.First (&msg); field.status == TIBMSG_OK; field.Next()) { 
					name.assign (field.Name(), field.NameSize() == 0 ? 0 : strlen (field.Name()));
			                memset (buf, 0, sizeof (buf));
			                if (field.Convert (buf, sizeof (buf)) != TIBMSG_OK) continue;
					const char* p = buf;
					std::ostringstream oss;
					bool has_comma = false;
					while (*p) {
						if (*p++ == ',') { 
							has_comma = true;
							break;
						}
					}
					if (has_comma)
						oss << '"' << buf << '"';
					else
						oss << buf;
					auto it = fid_map.find (name);
					if (it != fid_map.end())
						columns[it->second] = oss.str();
				}

				auto& fs = service_map[mfeed.service_name()];
				*fs << JoinString (columns, ',') << std::endl;
				++i;
			}
		}
		LOG(INFO) << i << " records recorded.";
	}
}

void
torikuru::torikuru_t::Clear()
{
/* Flush file streams */
	if (nullptr != coded_stream_) {
		delete coded_stream_;
		coded_stream_ = nullptr;
	}
	if (nullptr != gzip_stream_) {
		gzip_stream_->Close();
		delete gzip_stream_;
		gzip_stream_ = nullptr;
	}
	if (nullptr != output_stream_) {
		output_stream_->Close();
		delete output_stream_;
		output_stream_ = nullptr;
	}
	if (output_fd_ != -1) {
		close (output_fd_);
		output_fd_ = -1;
		LOG(INFO) << "Closed output file.";
	}

/* Signal message pump thread to exit. */
	if ((bool)event_queue_ && event_queue_->isActive())
		event_queue_->deactivate();

/* Purge subscription streams. */
	streams_.clear();

/* Release everything with an RFA dependency. */
	consumers_.clear();
	CHECK (log_.use_count() <= 1);
	log_.reset();
	CHECK (event_queue_.use_count() <= 1);
	event_queue_.reset();

/* Final tests before releasing RFA context */
	chromium::debug::LeakTracker<consumer_t>::CheckForLeaks();
	chromium::debug::LeakTracker<logging::rfa::LogEventProvider>::CheckForLeaks();

/* No more RFA handles so close up context */
	CHECK (rfa_.use_count() <= 1);
	rfa_.reset();

	chromium::debug::LeakTracker<rfa_t>::CheckForLeaks();
}

/* eof */
