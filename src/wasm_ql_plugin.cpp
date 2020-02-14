// copyright defined in LICENSE.txt

// todo: timeout wasm
// todo: timeout sql
// todo: what should memory size limit be?
// todo: check callbacks for recursion to limit stack size
// todo: reformulate get_input_data and set_output_data for reentrancy
// todo: wasms get whether a query is present
// todo: indexes on authorized, ram usage, notify
// todo: namespaces for queries
//          A standard namespace
//          ? one for the tokens
// todo: version on queries
//       vector<extendable<...>>
// todo: version on query api?
// todo: better naming for queries

#include "wasm_ql_plugin.hpp"
#include "wasm_ql.hpp"
#include "wasm_ql_http.hpp"

#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>

using namespace appbase;
using namespace wasm_ql;
using namespace std::literals;

static abstract_plugin& _wasm_ql_plugin = app().register_plugin<wasm_ql_plugin>();

struct wasm_ql_plugin_impl : std::enable_shared_from_this<wasm_ql_plugin_impl> {
   bool                                         stopping     = false;
   std::shared_ptr<const wasm_ql::http_config>  http_config  = {};
   std::shared_ptr<const wasm_ql::shared_state> shared_state = {};
   std::shared_ptr<wasm_ql::http_server>        http_server  = {};

   void start_http() { http_server = wasm_ql::http_server::create(http_config, shared_state); }

   void shutdown() {
      stopping = true;
      if (http_server)
         http_server->stop();
   }
}; // wasm_ql_plugin_impl

wasm_ql_plugin::wasm_ql_plugin() : my(std::make_shared<wasm_ql_plugin_impl>()) {}

wasm_ql_plugin::~wasm_ql_plugin() {
   if (my->stopping)
      ilog("wasm_ql_plugin stopped");
}

void wasm_ql_plugin::set_program_options(options_description& cli, options_description& cfg) {
   auto op = cfg.add_options();
   op("wql-threads", bpo::value<int>()->default_value(8), "Number of threads to process requests");
   op("wql-listen", bpo::value<std::string>()->default_value("127.0.0.1:8880"), "Endpoint to listen on");
   op("wql-allow-origin", bpo::value<std::string>(), "Access-Control-Allow-Origin header. Use \"*\" to allow any.");
   op("wql-contract-dir", bpo::value<std::string>(),
      "Directory to fetch contracts from. These override contracts on the chain. (default: disabled)");
   op("wql-static-dir", bpo::value<std::string>(), "Directory to serve static files from (default: disabled)");
   op("wql-console-size", bpo::value<uint32_t>()->default_value(0), "Maximum size of console data");
   op("wql-wasm-cache-size", bpo::value<uint32_t>()->default_value(100), "Maximum number of compiled wasms to cache");
   op("wql-max-request-size", bpo::value<uint32_t>()->default_value(10000), "HTTP maximum request body size (bytes)");
   op("wql-idle-timeout", bpo::value<uint32_t>()->default_value(30), "HTTP idle connection timeout (seconds)");
}

void wasm_ql_plugin::plugin_initialize(const variables_map& options) {
   try {
      auto ip_port = options.at("wql-listen").as<std::string>();
      if (ip_port.find(':') == std::string::npos)
         throw std::runtime_error("invalid --wql-listen value: " + ip_port);

      auto http_config  = std::make_shared<wasm_ql::http_config>();
      auto shared_state = std::make_shared<wasm_ql::shared_state>(app().find_plugin<rocksdb_plugin>()->get_db());
      my->http_config   = http_config;
      my->shared_state  = shared_state;

      http_config->num_threads       = options.at("wql-threads").as<int>();
      http_config->port              = ip_port.substr(ip_port.find(':') + 1, ip_port.size());
      http_config->address           = ip_port.substr(0, ip_port.find(':'));
      shared_state->max_console_size = options.at("wql-console-size").as<uint32_t>();
      shared_state->wasm_cache_size  = options.at("wql-wasm-cache-size").as<uint32_t>();
      http_config->max_request_size  = options.at("wql-max-request-size").as<uint32_t>();
      http_config->idle_timeout      = options.at("wql-idle-timeout").as<uint32_t>();
      if (options.count("wql-contract-dir"))
         shared_state->contract_dir = options.at("wql-contract-dir").as<std::string>();
      if (options.count("wql-allow-origin"))
         http_config->allow_origin = options.at("wql-allow-origin").as<std::string>();
      if (options.count("wql-static-dir"))
         http_config->static_dir = options.at("wql-static-dir").as<std::string>();

      register_callbacks();
   }
   FC_LOG_AND_RETHROW()
}

void wasm_ql_plugin::plugin_startup() { my->start_http(); }
void wasm_ql_plugin::plugin_shutdown() { my->shutdown(); }
