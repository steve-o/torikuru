/* RFA logging consumer.
 */

#ifndef __RFA_LOGGING_HH__
#define __RFA_LOGGING_HH__
#pragma once

/* Boost noncopyable base class */
#include <boost/utility.hpp>

/* RFA 7.2 */
#include <rfa/rfa.hh>

#include "chromium/debug/leak_tracker.hh"
#include "config.hh"
#include "deleter.hh"

namespace logging
{
namespace rfa
{

	class LogEventProvider :
		public ::rfa::common::Client,
		boost::noncopyable
	{
	public:
		LogEventProvider (const torikuru::config_t& config, std::shared_ptr<::rfa::common::EventQueue> event_queue);
		~LogEventProvider ();

		bool Register () throw (::rfa::common::InvalidUsageException, ::rfa::common::InvalidConfigurationException);
		bool Unregister();

/* RFA event callback. */
		void processEvent (const ::rfa::common::Event& event_) override;

	private:
		void OnLoggerNotifyEvent (const ::rfa::logger::LoggerNotifyEvent& event_);

		const torikuru::config_t& config_;

/* RFA event queue. */
		std::shared_ptr<::rfa::common::EventQueue> event_queue_;

/* RFA "application logger", a logging transport. */
		std::unique_ptr<::rfa::logger::ApplicationLogger, internal::release_deleter> logger_;

/* RFA "application logger monitor", an RFA event source. */
		std::unique_ptr<::rfa::logger::AppLoggerMonitor, internal::destroy_deleter> monitor_;

/* RFA log event consumer. */
		::rfa::common::Handle* handle_;

		chromium::debug::LeakTracker<LogEventProvider> leak_tracker_;
	};

} /* namespace rfa */

} /* namespace logging */

#endif /* __RFA_LOGGING_HH__ */

/* eof */