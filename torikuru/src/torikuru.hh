/* RFA subscriber.
 *
 */

#ifndef __TORIKURU_HH__
#define __TORIKURU_HH__

#include <cstdint>

/* Boost noncopyable base class */
#include <boost/utility.hpp>

/* Boost threading. */
#include <boost/thread.hpp>

/* RFA 7.2 */
#include <rfa/rfa.hh>

/* Protocol Buffers */
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/gzip_stream.h>

#include "config.hh"
#include "consumer.hh"

namespace logging
{
namespace rfa
{
	class LogEventProvider;
}
}

namespace torikuru
{
	class rfa_t;
	class consumer_t;

/* Basic example structure for application state of an item stream. */
	class subscription_stream_t : public item_stream_t
	{
	public:
		subscription_stream_t ()
		{
		}
	};

	class torikuru_t :
		boost::noncopyable
	{
	public:
		torikuru_t ();
		~torikuru_t();

		bool Init();
/* Run the consumer with the given command-line parameters.
 * Returns the error code to be returned by main().
 */
		int Run();
		void Clear();

/* Global list of all application instances. */
		static std::list<torikuru_t*> global_list_;
		static boost::shared_mutex global_list_lock_;

	protected:
/* Run core event loop. */
		void MainLoop();

/* ETL process. */
		void Convert();

/* Application configuration. */
		config_t config_;

/* RFA context. */
		std::shared_ptr<rfa_t> rfa_;

/* RFA asynchronous event queue. */
		std::shared_ptr<rfa::common::EventQueue> event_queue_;

/* RFA logging */
		std::shared_ptr<logging::rfa::LogEventProvider> log_;

/* RFA consumer */
		std::list<std::shared_ptr<consumer_t>> consumers_;
		unsigned consumers_in_sync_;
	
/* Item stream. */
		std::list<std::shared_ptr<subscription_stream_t>> streams_;

/* Update fields. */
		rfa::data::FieldList fields_;

/* File streams */
		int output_fd_;
		google::protobuf::io::FileOutputStream* output_stream_;
		google::protobuf::io::GzipOutputStream* gzip_stream_;
		google::protobuf::io::CodedOutputStream* coded_stream_;
	};

} /* namespace torikuru */

#endif /* __TORIKURU_HH__ */

/* eof */
