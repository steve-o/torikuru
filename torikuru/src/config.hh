/* User-configurable settings.
 *
 * NB: all strings are locale bound, RFA provides no Unicode support.
 */

#ifndef __CONFIG_HH__
#define __CONFIG_HH__
#pragma once

#include <string>
#include <sstream>
#include <vector>

namespace torikuru
{

	struct session_config_t
	{
//  RFA session name, one session contains a horizontal scaling set of connections.
		std::string session_name;

//  RFA connection name, used for logging.
		std::string connection_name;

//  RFA consumer name.
		std::string consumer_name;

//  Protocol name, RSSL or SSL.
		std::string protocol;

//  TREP-RT service name, e.g. IDN_RDF.
		std::string service_name;

//  TREP-RT ADH hostname or IP address.
		std::vector<std::string> servers;

//  Default TREP-RT R/SSL port, e.g. 14002, 14003, 8101.
		std::string default_port;

/* DACS application Id.  If the server authenticates with DACS, the consumer
 * application may be required to pass in a valid ApplicationId.
 * Range: "" (None) or 1-511 as an Ascii string.
 */
		std::string application_id;

/* InstanceId is used to differentiate applications running on the same host.
 * If there is more than one noninteractive provider instance running on the
 * same host, they must be set as a different value by the provider
 * application. Otherwise, the infrastructure component which the providers
 * connect to will reject a login request that has the same InstanceId value
 * and cut the connection.
 * Range: "" (None) or any Ascii string, presumably to maximum RFA_String length.
 */
		std::string instance_id;

/* DACS username, frequently non-checked and set to similar: user1.
 */
		std::string user_name;

/* DACS position, the station which the user is using.
 * Range: "" (None) or "<IPv4 address>/hostname" or "<IPv4 address>/net"
 */
		std::string position;
	};

	struct config_t
	{
		config_t();

//  RFA sessions comprising of session names, connection names,
//  RSSL hostname or IP address and default RSSL port, e.g. 14002, 14003.
		std::vector<session_config_t> sessions;

//  Subscription list.
		std::vector<std::string> instruments;

//  Only retrieve initial images
		bool disable_update;

//  Only store updates
		bool disable_refresh;

//  Finish capture when all symbols return a refresh or status close.
		bool terminate_on_sync;

//  File containing list of symbols
		std::string symbol_path;

//  Where to record images
		std::string output_path;

//  Where to read images from
		std::string input_path;

//  Time period to capture data, in seconds.
		std::string time_limit;

//// API boiler plate nomenclature
//  RFA application logger monitor name.
		std::string monitor_name;

//  RFA event queue name.
		std::string event_queue_name;
	};

	inline
	std::ostream& operator<< (std::ostream& o, const session_config_t& session) {
		o << "{ "
			  "\"session_name\": \"" << session.session_name << "\""
			", \"connection_name\": \"" << session.connection_name << "\""
			", \"consumer_name\": \"" << session.consumer_name << "\""
			", \"protocol\": \"" << session.protocol << "\""
			", \"service_name\": \"" << session.service_name << "\""
			", \"default_port\": \"" << session.default_port << "\""
			", \"application_id\": \"" << session.application_id << "\""
			", \"instance_id\": \"" << session.instance_id << "\""
			", \"user_name\": \"" << session.user_name << "\""
			", \"position\": \"" << session.position << "\""
			" }";
		return o;
	}

	inline
	std::ostream& operator<< (std::ostream& o, const config_t& config) {
		std::ostringstream sessions, instruments;
		for (auto it = config.sessions.begin(); it != config.sessions.end(); ++it) {
			if (it != config.sessions.begin())
				sessions << ", ";
			sessions << '"' << *it << '"';
		}		
		for (auto it = config.instruments.begin(); it != config.instruments.end(); ++it) {
			if (it != config.instruments.begin())
				instruments << ", ";
			instruments << '"' << *it << '"';
		}		
		o << "\"config_t\": { "
			  "\"sessions\": [" << sessions.str() << "]"
			", \"instruments\": \"" << instruments.str() << "\""
			", \"disable_update\": " << (config.disable_update?"true":"false") << ""
			", \"disable_refresh\": " << (config.disable_refresh?"true":"false") << ""
			", \"terminate_on_sync\": " << (config.terminate_on_sync?"true":"false") << ""
			", \"symbol_path\": \"" << config.symbol_path << "\""
			", \"output_path\": \"" << config.output_path << "\""
			", \"input_path\": \"" << config.input_path << "\""
			", \"time_limit\": \"" << config.time_limit << "\""
			", \"monitor_name\": \"" << config.monitor_name << "\""
			", \"event_queue_name\": \"" << config.event_queue_name << "\""
			" }";
		return o;
	}

} /* namespace torikuru */

#endif /* __CONFIG_HH__ */

/* eof */
