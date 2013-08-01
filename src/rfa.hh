/* RFA context.
 */

#ifndef __RFA_HH__
#define __RFA_HH__
#pragma once

/* Boost noncopyable base class */
#include <boost/utility.hpp>

/* RFA 7.2 */
#include <rfa/rfa.hh>

#include "chromium/debug/leak_tracker.hh"
#include "config.hh"
#include "deleter.hh"

namespace connections {
extern const char kSSLED[];
extern const char kRSSL[];
}

namespace torikuru
{
	class rfa_t :
		boost::noncopyable
	{
	public:
		rfa_t (const config_t& config);
		~rfa_t();

		bool Init() throw (rfa::common::InvalidUsageException);
		bool VerifyVersion();

	private:
		const config_t& config_;		

/* Live config database */
		std::unique_ptr<rfa::config::ConfigDatabase, internal::release_deleter> rfa_config_;

		chromium::debug::LeakTracker<rfa_t> leak_tracker_;
	};

} /* namespace torikuru */

#endif /* __RFA_HH__ */

/* eof */
