#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <string>
#include <vector>

#include <curl/curl.h>
#include "json.hpp"
#include "httplib.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

// Structs from original implementation
struct MarketDetails
{
    std::string coindcx_name;
    std::string base_currency_short_name;
    std::string target_currency_short_name;
    std::string target_currency_name;
    std::string base_currency_name;
    double min_quantity;
    double max_quantity;
    double min_price;
    double max_price;
    double min_notional;
    int base_currency_precision;
    int target_currency_precision;
    float step;
    std::vector<std::string> order_types;
    std::string symbol;
    std::string ecode;
    std::string max_leverage;
    std::string pair;
    std::string status;
};

struct TickerDetails
{
    std::string market;
    std::string change_24_hour;
    std::string high;
    std::string low;
    std::string volume;
    std::string last_price;
    std::string bid;
    std::string ask;
    long long timestamp;

    // Default Constructor
    TickerDetails()
        : market(""),
          change_24_hour("0"),
          high(""),
          low(""),
          volume(""),
          last_price(""),
          bid(""),
          ask(""),
          timestamp(0)
    {}

    // Parameterized Constructor
    TickerDetails(
        const std::string& market_,
        const std::string& change_24_hour_,
        const std::string& high_,
        const std::string& low_,
        const std::string& volume_,
        const std::string& last_price_,
        const std::string& bid_,
        const std::string& ask_,
        long long timestamp_)
        : market(market_),
          change_24_hour(change_24_hour_),
          high(high_),
          low(low_),
          volume(volume_),
          last_price(last_price_),
          bid(bid_),
          ask(ask_),
          timestamp(timestamp_)
    {}
};


struct OrderBook
{
    std::unordered_map<std::string, std::string> bids;
    std::unordered_map<std::string, std::string> asks;
};

class ConfigManager
{
public:
    static ConfigManager &getInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    void loadFromFile(const std::string &filename)
    {
        try
        {
            std::ifstream file(filename);
            if (file.is_open())
            {
                json config = json::parse(file);
                api_base_url = config.value("api_base_url", "https://api.coindcx.com");
                max_retries = config.value("max_retries", 3);
                retry_delay_ms = config.value("retry_delay_ms", 1000);
                log_level = config.value("log_level", "info");
                port = config.value("port", 8080);
                host = config.value("host", "localhost");
            }
        }
        catch (const std::exception &e)
        {
            std::clog << "Config load error: {}" << e.what() << std::endl;
        }
    }

    std::string api_base_url;
    int max_retries;
    int retry_delay_ms;
    std::string log_level;
    int port;
    std::string host;

private:
    ConfigManager() : api_base_url("https://api.coindcx.com"),
                      max_retries(3),
                      retry_delay_ms(1000),
                      log_level("info"),
                      port(8080),
                      host("localhost") {}
};

class SafeCurlWrapper
{
private:
    CURL *curl;
    std::string response_buffer;
    std::mutex curl_mutex;

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        static_cast<std::string *>(userp)->append(
            static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }

public:
    SafeCurlWrapper() : curl(nullptr)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~SafeCurlWrapper()
    {
        if (curl)
            curl_easy_cleanup(curl);
        curl_global_cleanup();
    }

    std::string performRequest(const std::string &url)
    {
        std::lock_guard<std::mutex> lock(curl_mutex);

        curl = curl_easy_init();
        if (!curl)
        {
            // spdlog::error("CURL initialization failed");
            return "";
        }

        response_buffer.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            // spdlog::error("CURL request failed: {}", curl_easy_strerror(res));
            return "";
        }

        return response_buffer;
    }
};

class CryptoTracker
{
private:
    SafeCurlWrapper curl_wrapper;
    std::unordered_map<std::string, MarketDetails> market_details_map;
    std::unordered_map<std::string, TickerDetails> ticker_details_map;
    std::unordered_map<std::string, OrderBook> order_book_map;
    std::unordered_map<std::string, std::string> market_pairs_map;
    std::atomic<bool> is_running{false};

public:
    void startBackgroundRefresh()
    {
        is_running = true;
        std::thread([this]()
                    {
            while (is_running) {
                try {
                    refreshTickerData();
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
                catch (const std::exception &e)
                {
                    std::clog << "Background refresh error: " << e.what() << std::endl;
                }
            } })
            .detach();
    }

    void stopBackgroundRefresh()
    {
        is_running = false;
    }

    void refreshMarketData()
    {
        const std::string url = ConfigManager::getInstance().api_base_url + "/exchange/v1/markets_details";
        std::clog << url << std::endl;
        std::string response = curl_wrapper.performRequest(url);
        // std::clog<<response<<std::endl;
        if (!response.empty())
        {
            auto market_details = parseMarketDetails(response);
            updateMarketDetailsMap(market_details);
        }
    }

    void refreshTickerData()
    {
        const std::string url = ConfigManager::getInstance().api_base_url + "/exchange/ticker";
        std::string response = curl_wrapper.performRequest(url);

        if (!response.empty())
        {
            auto ticker_details = parseTickerDetails(response);
            updateTickerDetailsMap(ticker_details);
        }
    }

    void getOrderBook(const std::string &pair)
    {
        const std::string url = "https://public.coindcx.com/market_data/orderbook?pair=" + pair;
        std::string response = curl_wrapper.performRequest(url);

        if (!response.empty())
        {
            OrderBook order_book = parseOrderBook(response);
            order_book_map[pair] = order_book;
        }
    }

    json handleDataRequest(const std::string &market_name)
    {
        json response_json;
        std::string pair;

        if (market_pairs_map.find(market_name) != market_pairs_map.end())
        {
            pair = market_pairs_map[market_name];
            std::clog << pair << std::endl;
            getOrderBook(pair);

            response_json["pair"] = market_name;
            response_json["request_timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            response_json["order_book"] = {
                {"bids", convertToJson(order_book_map[pair].bids)},
                {"asks", convertToJson(order_book_map[pair].asks)}};
            addMarketDetails(response_json, market_name);
        }

        return response_json;
    }

    std::vector<std::string> getAllPairs()
    {
        std::vector<std::string> pairs;
        for (const auto &[pair, _] : market_pairs_map)
        {
            pairs.push_back(pair);
        }
        return pairs;
    }
    std::vector<TickerDetails> getAllTickerData()
    {
        std::vector<TickerDetails> tickerData;
        for (const auto &[ticker, _] : ticker_details_map)
        {
            tickerData.push_back(_);
        }
        return tickerData;
    }

private:
    std::vector<MarketDetails> parseMarketDetails(const std::string &response);
    std::vector<TickerDetails> parseTickerDetails(const std::string &response);
    OrderBook parseOrderBook(const std::string &response);

    void updateMarketDetailsMap(const std::vector<MarketDetails> &market_details)
    {
        for (const auto &detail : market_details)
        {
            market_details_map[detail.coindcx_name] = detail;
            market_pairs_map[detail.coindcx_name] = detail.pair;
        }
    }

    void updateTickerDetailsMap(const std::vector<TickerDetails> &ticker_details)
    {
        for (const auto &detail : ticker_details)
        {
            if (detail.market == "BTCINR_insta")
            {
                continue;
            }
            ticker_details_map[detail.market] = detail;
        }
    }

    json convertToJson(const std::unordered_map<std::string, std::string> &map)
    {
        json json_map = json::object();
        for (const auto &[key, value] : map)
        {
            json_map[key] = value;
        }
        return json_map;
    }

    void addMarketDetails(json &response_json, const std::string &market_name)
    {
        if (market_details_map.find(market_name) != market_details_map.end())
        {
            const auto &market_detail = market_details_map[market_name];
            response_json["market_details"] = {
                {"base_currency", market_detail.base_currency_short_name},
                {"target_currency", market_detail.target_currency_short_name},
                {"min_quantity", market_detail.min_quantity},
                {"max_quantity", market_detail.max_quantity},
                {"min_price", market_detail.min_price},
                {"max_price", market_detail.max_price}};
        }
    }

    void addTickerDetails(json &response_json, const std::string &market_name)
    {
        if (ticker_details_map.find(market_name) != ticker_details_map.end())
        {
            const auto &ticker_detail = ticker_details_map[market_name];
            response_json["ticker_details"] = {
                {"change_24_hour", ticker_detail.change_24_hour},
                {"last_price", ticker_detail.last_price},
                {"bid", ticker_detail.bid},
                {"ask", ticker_detail.ask},
                {"high", ticker_detail.high},
                {"low", ticker_detail.low},
                {"volume", ticker_detail.volume},
                {"timestamp", ticker_detail.timestamp}};
        }
    }
};

std::vector<MarketDetails> CryptoTracker::parseMarketDetails(const std::string &response)
{
    auto json_data = json::parse(response);
    std::vector<MarketDetails> market_details;

    for (const auto &item : json_data)
    {
        MarketDetails details;
        details.coindcx_name = item["coindcx_name"];
        details.base_currency_short_name = item["base_currency_short_name"];
        details.target_currency_short_name = item["target_currency_short_name"];
        details.target_currency_name = item["target_currency_name"];
        details.base_currency_name = item["base_currency_name"];
        details.min_quantity = item["min_quantity"];
        details.max_quantity = item["max_quantity"];
        details.min_price = item["min_price"];
        details.max_price = item["max_price"];
        details.min_notional = item["min_notional"];
        details.base_currency_precision = item["base_currency_precision"];
        details.target_currency_precision = item["target_currency_precision"];
        details.step = item["step"];
        details.order_types = item["order_types"].get<std::vector<std::string>>();
        details.symbol = item["symbol"];
        details.ecode = item["ecode"];
        details.pair = item["pair"];
        details.status = item["status"];

        market_details.push_back(details);
    }

    return market_details;
}

OrderBook CryptoTracker::parseOrderBook(const std::string &response)
{
    OrderBook order_book;

    try
    {
        auto json_data = json::parse(response);

        if (json_data.contains("bids") && json_data["bids"].is_object())
        {
            for (auto &[price, quantity] : json_data["bids"].items())
            {
                order_book.bids[price] = quantity.get<std::string>();
            }
        }

        if (json_data.contains("asks") && json_data["asks"].is_object())
        {
            for (auto &[price, quantity] : json_data["asks"].items())
            {
                order_book.asks[price] = quantity.get<std::string>();
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing order book JSON: " << e.what() << std::endl;
    }

    return order_book;
}

std::vector<TickerDetails> CryptoTracker::parseTickerDetails(const std::string &response)
{
    auto json_data = json::parse(response);
    std::vector<TickerDetails> ticker_details;

    for (const auto &item : json_data)
    {
        TickerDetails details;

        // Check and safely assign each field
        if (item.contains("market") && item["market"].is_string())
        {
            details.market = item["market"].get<std::string>();
        }

        if (item.contains("change_24_hour"))
        {
            if (item["change_24_hour"].is_number())
            {
                details.change_24_hour = std::to_string(item["change_24_hour"].get<double>());
            }
            else if (item["change_24_hour"].is_string())
            {
                details.change_24_hour = item["change_24_hour"].get<std::string>();
            }
        }

        if (item.contains("high"))
        {
            if (item["high"].is_number())
            {
                details.high = std::to_string(item["high"].get<double>());
            }
            else if (item["high"].is_string())
            {
                details.high = item["high"].get<std::string>();
            }
        }

        if (item.contains("low"))
        {
            if (item["low"].is_number())
            {
                details.low = std::to_string(item["low"].get<double>());
            }
            else if (item["low"].is_string())
            {
                details.low = item["low"].get<std::string>();
            }
        }

        if (item.contains("volume"))
        {
            if (item["volume"].is_number())
            {
                details.volume = std::to_string(item["volume"].get<double>());
            }
            else if (item["volume"].is_string())
            {
                details.volume = item["volume"].get<std::string>();
            }
        }

        if (item.contains("last_price"))
        {
            if (item["last_price"].is_number())
            {
                details.last_price = std::to_string(item["last_price"].get<double>());
            }
            else if (item["last_price"].is_string())
            {
                details.last_price = item["last_price"].get<std::string>();
            }
        }

        if (item.contains("bid"))
        {
            if (item["bid"].is_number())
            {
                details.bid = std::to_string(item["bid"].get<double>());
            }
            else if (item["bid"].is_string())
            {
                details.bid = item["bid"].get<std::string>();
            }
        }

        if (item.contains("ask"))
        {
            if (item["ask"].is_number())
            {
                details.ask = std::to_string(item["ask"].get<double>());
            }
            else if (item["ask"].is_string())
            {
                details.ask = item["ask"].get<std::string>();
            }
        }

        if (item.contains("timestamp") && item["timestamp"].is_number())
        {
            details.timestamp = item["timestamp"].get<long long>();
        }

        // Add the parsed details to the vector
        ticker_details.push_back(details);
    }

    return ticker_details;
}
class CryptoAPIServer
{
private:
    httplib::Server server;
    CryptoTracker crypto_tracker;

public:
    void start()
    {
        auto &config = ConfigManager::getInstance();
        crypto_tracker.refreshMarketData();
        crypto_tracker.refreshTickerData();
        crypto_tracker.startBackgroundRefresh();

        server.Post("/livedata", [this](const httplib::Request &req, httplib::Response &res)
                    {
            try {
                if (!req.has_param("symbol")) {
                    throw std::runtime_error("Missing 'symbol' parameter");
                }

                std::string market_name = req.get_param_value("symbol");
                json response_json = crypto_tracker.handleDataRequest(market_name);

                res.set_header("Content-Type", "application/json");
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(response_json.dump(2), "application/json");
            } catch (const std::exception& e) {
                std::cout<< "Server error: " <<  e.what()<<std::endl;
                res.status = 500;
                json error_json = {
                    {"error", e.what()},
                    {"request_timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                };
                res.set_content(error_json.dump(2), "application/json");
            } });

        server.Get("/pairs", [this](const httplib::Request &req, httplib::Response &res)
                   {
            try {
                std::vector<std::string> pairs = crypto_tracker.getAllPairs();
                json response_json = {{"pairs", pairs}};

                res.set_header("Content-Type", "application/json");
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(response_json.dump(2), "application/json");
            } catch (const std::exception& e) {
                std::cout<< "Server error: " <<  e.what()<<std::endl;
                res.status = 500;
                json error_json = {
                    {"error", e.what()},
                    {"request_timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                };
                res.set_content(error_json.dump(2), "application/json");
            } });

        server.Get("/ticker", [this](const httplib::Request &req, httplib::Response &res)
                   {
            try {
                std::vector<TickerDetails> tickerData = crypto_tracker.getAllTickerData();

                 nlohmann::json response_json;
                 response_json = nlohmann::json::array();

                 for (const auto &td : tickerData)
                 {
                     std::cout<< td.market << " " << td.last_price << std::endl;
                     response_json.push_back({{"symbol", td.market},
                                              {"last_traded_price", td.last_price},
                                              {"volume", td.volume},
                                              {"exchange_timestamp", td.timestamp},
                                              {"ask", td.ask},
                                              {"bid", td.bid},
                                              {"high", td.high},
                                              {"low", td.low},
                                              {"change_24_hour",td.change_24_hour},
                                              {"request_timestamp", std::chrono::system_clock::now().time_since_epoch().count()}});
                 }


                res.set_header("Content-Type", "application/json");
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(response_json.dump(2), "application/json");
            } catch (const std::exception& e) {
                std::cout<< "Server error: " <<  e.what()<<std::endl;
                res.status = 500;
                json error_json = {
                    {"error", e.what()},
                    {"request_timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                };
                res.set_content(error_json.dump(2), "application/json");
            } });

        std::cout << "Server starting on " << config.host << ":" << config.port << "\n";
        server.listen(config.host.c_str(), config.port);
    }
};

int main()
{
    // Setup logging
    // spdlog::set_level(spdlog::level::info);

    // Load configuration
    ConfigManager::getInstance().loadFromFile("config.json");

    CryptoAPIServer server;

    server.start();

    return 0;
}