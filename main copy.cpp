#include <iostream>
#include "json.hpp"
#include <curl/curl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "ScheduleTimeExecute.hpp"
#include <future>
#include "httplib.h"

using json = nlohmann::json;

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
};

struct OrderBook
{
    std::unordered_map<std::string, std::string> bids;
    std::unordered_map<std::string, std::string> asks;
};

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

class CryptoTracker
{
private:
    CURL *CurlGetHndl;
    struct curl_slist *headers = nullptr;
    std::string CurlResponse;
    std::unordered_map<std::string, MarketDetails> market_details_map;
    std::unordered_map<std::string, TickerDetails> ticker_details_map;
    std::unordered_map<std::string, OrderBook> order_book_map;
    std::unordered_map<std::string,std::string> market_pairs_map;

public:
    short initialize_curl_handle();
    std::vector<MarketDetails> parseMarketDetails(const std::string &response);
    std::vector<TickerDetails> parseTickerDetails(const std::string &response);
    OrderBook parseOrderBook(const std::string &response);
    void getMarketDetails();
    void getTickerDetails();
    void getOrderBook(const std::string &pair);
    void handle_data(const httplib::Request &req, httplib::Response &res);

    CryptoTracker();
    ~CryptoTracker(); // Destructor to clean up curl_slist
};

CryptoTracker::CryptoTracker() {}

CryptoTracker::~CryptoTracker()
{
    if (headers)
    {
        curl_slist_free_all(headers);
    }
    if (CurlGetHndl)
    {
        curl_easy_cleanup(CurlGetHndl);
    }
}

short CryptoTracker::initialize_curl_handle()
{
    CurlGetHndl = curl_easy_init();
    if (!CurlGetHndl)
        return 1;

    headers = curl_slist_append(headers, "content-type: application/json");
    curl_easy_setopt(CurlGetHndl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(CurlGetHndl, CURLOPT_HTTPHEADER, headers);

    // Set the write callback function to capture response data
    curl_easy_setopt(CurlGetHndl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(CurlGetHndl, CURLOPT_WRITEDATA, &CurlResponse);

    return 0;
}

void CryptoTracker::getMarketDetails()
{
    CURLcode res;
    const std::string url = "https://api.coindcx.com/exchange/v1/markets_details";
    curl_easy_setopt(CurlGetHndl, CURLOPT_URL, url.c_str());
    CurlResponse.clear();
    res = curl_easy_perform(CurlGetHndl);

    if (res != CURLE_OK)
    {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
    }
    else
    {
        auto market_details = parseMarketDetails(CurlResponse);
        for (const auto &detail : market_details)
        {
            market_details_map[detail.base_currency_short_name + detail.target_currency_short_name] = detail;
            market_pairs_map[detail.coindcx_name] = detail.pair;
        }

    }

    for (const auto &[pair, detail] : market_details_map)
    {

        std::cout << "Pair " << pair << std::endl;
        std::cout << "CoinDCX Name: " << detail.coindcx_name << std::endl;
        std::cout << "Base Currency: " << detail.base_currency_name << " (" << detail.base_currency_short_name << ")" << std::endl;
        std::cout << "Target Currency: " << detail.target_currency_name << " (" << detail.target_currency_short_name << ")" << std::endl;
        std::cout << "Min Quantity: " << detail.min_quantity << std::endl;
        std::cout << "Max Quantity: " << detail.max_quantity << std::endl;
        std::cout << "Min Price: " << detail.min_price << std::endl;
        std::cout << "Max Price: " << detail.max_price << std::endl;
        std::cout << "Order Types: ";
        for (const auto &order_type : detail.order_types)
        {
            std::cout << order_type << " ";
        }
        std::cout << std::endl;
        std::cout << "Status: " << detail.status << std::endl
                  << std::endl;
        //  getOrderBook(detail.pair);
    }
}

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

void CryptoTracker::getTickerDetails()
{
    CURLcode res;
    const std::string url = "https://api.coindcx.com/exchange/ticker";
    curl_easy_setopt(CurlGetHndl, CURLOPT_URL, url.c_str());
    CurlResponse.clear();
    res = curl_easy_perform(CurlGetHndl);

    if (res != CURLE_OK)
    {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
    }
    else
    {
        auto ticker_details = parseTickerDetails(CurlResponse);
        for (const auto &detail : ticker_details)
        {
            ticker_details_map[detail.market] = detail;
        }
    }
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

void CryptoTracker::getOrderBook(const std::string &pair)
{
    CURLcode res;
    const std::string url = "https://public.coindcx.com/market_data/orderbook?pair=" + pair;
    curl_easy_setopt(CurlGetHndl, CURLOPT_URL, url.c_str());
    CurlResponse.clear();
    res = curl_easy_perform(CurlGetHndl);

    if (res != CURLE_OK)
    {
        std::cerr << "CURL request failed: " << curl_easy_strerror(res) << std::endl;
    }
    else
    {
        auto order_book = parseOrderBook(CurlResponse);
        order_book_map[pair] = order_book;

        std::cout << "Order Book for " << pair << ":" << std::endl;

        std::cout << "Bids:" << std::endl;
        for (const auto &[price, quantity] : order_book.bids)
        {
            std::cout << "Price: " << price << ", Quantity: " << quantity << std::endl;
        }

        std::cout << "Asks:" << std::endl;
        for (const auto &[price, quantity] : order_book.asks)
        {
            std::cout << "Price: " << price << ", Quantity: " << quantity << std::endl;
        }
    }
}

void CryptoTracker::handle_data(const httplib::Request &req, httplib::Response &res)
{
    if (req.has_param("data"))
    {
        std::string data = req.get_param_value("data");
        std::cout << "Received data: " << data << std::endl;
        std::string pair;
        if (data.empty())
        {
        }
        else if (market_pairs_map.find(data) != market_pairs_map.end())
        {
            pair = market_pairs_map[data];
            std::cout << pair << std::endl;
        }
        // Get latest order book data
        getOrderBook(pair);

        // Create JSON response with bids and asks
        json response_json;
        response_json["pair"] = data;

        // Add bids to response
        json bids_json = json::object();
        for (const auto &[price, quantity] : order_book_map[pair].bids)
        {
            bids_json[price] = quantity;
        }
        response_json["bids"] = bids_json;

        // Add asks to response
        json asks_json = json::object();
        for (const auto &[price, quantity] : order_book_map[pair].asks)
        {
            asks_json[price] = quantity;
        }
        response_json["asks"] = asks_json;

        // Add timestamp
        response_json["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

        // Add extra market details if available
        if (market_details_map.find(data) != market_details_map.end())
        {
            const auto &market_detail = market_details_map[data];
            response_json["market_details"] = {
                {"base_currency", market_detail.base_currency_short_name},
                {"target_currency", market_detail.target_currency_short_name},
                {"min_quantity", market_detail.min_quantity},
                {"max_quantity", market_detail.max_quantity},
                {"min_price", market_detail.min_price},
                {"max_price", market_detail.max_price}};
        }

        if (ticker_details_map.find(data) != ticker_details_map.end())
        {
            const auto &ticker_deatil = ticker_details_map[data];
            response_json["ticker_details"] = {
                {"change_24_hour", ticker_deatil.change_24_hour},
                {"last_price", ticker_deatil.last_price},
                {"bid", ticker_deatil.bid},
                {"ask", ticker_deatil.ask},
                {"high", ticker_deatil.high},
                {"low", ticker_deatil.low},
                {"volume", ticker_deatil.volume},
                {"timestamp", ticker_deatil.timestamp}};
        }

        // Set response headers
        res.set_header("Content-Type", "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");

        // Send response
        res.set_content(response_json.dump(2), "application/json");
    }
    else
    {
        json error_json = {
            {"status", "error"},
            {"message", "Missing 'data' parameter. Please provide a valid trading pair."},
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}};

        res.status = 400; // Bad request
        res.set_header("Content-Type", "application/json");
        res.set_content(error_json.dump(2), "application/json");
    }
}

void CryptoTracker::handle_get_pairs(const httplib::Request& req, httplib::Response& res) {
    // Create a JSON response to list all available market pairs
    json response_json;

    // Iterate through the market_pairs_map and add each pair to the response
    for (const auto& [pair, _] : market_pairs_map) {
        response_json["pairs"].push_back(pair);
    }

    // Set response headers
    res.set_header("Content-Type", "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");

    // Send response
    res.set_content(response_json.dump(2), "application/json");
}

int main()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    CryptoTracker Tracker;
    Tracker.initialize_curl_handle();
    Tracker.getMarketDetails();
    Tracker.getTickerDetails();
    auto task = std::bind(&CryptoTracker::getMarketDetails, &Tracker);

    auto handle_data = [&Tracker](const httplib::Request &req, httplib::Response &res)
    {
        Tracker.handle_data(req, res); // Call the member function on the instance
    };

    Scheduler scheduler(5);
    scheduler.start(task);

    httplib::Server svr;

    // Define routes and bind them to respective functions
    svr.Post("/data", handle_data);
    svr.Get("/pairs", [&Tracker](const httplib::Request &req, httplib::Response &res)
    {
        Tracker.handle_get_pairs(req, res);
    });

    std::cout << "Server is running on http://localhost:8080" << std::endl;
    svr.listen("localhost", 8080);

    // std::promise<void> exit_signal;
    // std::future<void> future = exit_signal.get_future();

    // exit_signal.set_value();
    // future.wait();

    scheduler.stop();
    curl_global_cleanup();

    return 0;
} how to optimze this code for proudction leval usage 
write full code 


g++ -std=c++17 -o crypto_tracker.exe crypto_tracker.cpp -lcurl -lws2_32 -lcrypt32
