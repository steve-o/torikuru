/* User-configurable settings.
 */

#include "config.hh"

torikuru::config_t::config_t() :
/* default values */
	disable_update (false),
	disable_refresh (false),
	terminate_on_sync (false),
/* boiler plate naming */
	monitor_name ("ApplicationLoggerMonitorName"),
	event_queue_name ("EventQueueName")
{
}

/* eof */
