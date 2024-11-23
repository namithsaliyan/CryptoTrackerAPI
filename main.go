package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

// ConfigManager handles application configuration
type ConfigManager struct {
	APIBaseURL  string
	MaxRetries  int
	RetryDelay  int
	LogLevel    string
	Port        int
	Host        string
}

var config ConfigManager

// Load configuration from file
func loadConfig(filename string) error {
	data, err := ioutil.ReadFile(filename)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, &config)
}

// MarketDetails struct to hold market information
type MarketDetails struct {
	CoindcxName             string   `json:"coindcx_name"`
	BaseCurrencyShortName   string   `json:"base_currency_short_name"`
	TargetCurrencyShortName string   `json:"target_currency_short_name"`
	TargetCurrencyName      string   `json:"target_currency_name"`
	BaseCurrencyName        string   `json:"base_currency_name"`
	MinQuantity             float64  `json:"min_quantity"`
	MaxQuantity             float64  `json:"max_quantity"`
	MinPrice                float64  `json:"min_price"`
	MaxPrice                float64  `json:"max_price"`
	MinNotional             float64  `json:"min_notional"`
	BaseCurrencyPrecision   int      `json:"base_currency_precision"`
	TargetCurrencyPrecision int      `json:"target_currency_precision"`
	Step                    float64  `json:"step"`
	OrderTypes              []string `json:"order_types"`
	Symbol                  string   `json:"symbol"`
	ECode                   string   `json:"ecode"`
	Pair                    string   `json:"pair"`
	Status                  string   `json:"status"`
}

// TickerDetails struct to hold ticker information
type TickerDetails struct {
	Market       string          `json:"market"`
	Change24Hour string          `json:"change_24_hour"`
	High         string          `json:"high"`
	Low          string          `json:"low"`
	Volume       string          `json:"volume"`
	LastPrice    string          `json:"last_price"`
	Bid          json.RawMessage `json:"bid"`
	Ask          json.RawMessage `json:"ask"`
	Timestamp    int64           `json:"timestamp"`
}

// OrderBook struct to hold order book details
type OrderBook struct {
	Bids map[string]string `json:"bids"`
	Asks map[string]string `json:"asks"`
}

// SafeHTTPClient wraps http.Client with thread safety
type SafeHTTPClient struct {
	client *http.Client
	mutex  sync.Mutex
}

func newSafeHTTPClient() *SafeHTTPClient {
	return &SafeHTTPClient{client: &http.Client{}}
}

func (c *SafeHTTPClient) performRequest(url string) (string, error) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	resp, err := c.client.Get(url)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	return string(body), nil
}

// CryptoTracker struct to manage crypto data
type CryptoTracker struct {
	httpClient    *SafeHTTPClient
	marketDetails map[string]MarketDetails
	tickerDetails map[string]TickerDetails
	orderBooks    map[string]OrderBook
	marketPairs   map[string]string
	isRunning     bool
	mutex         sync.RWMutex
}

func newCryptoTracker() *CryptoTracker {
	return &CryptoTracker{
		httpClient:    newSafeHTTPClient(),
		marketDetails: make(map[string]MarketDetails),
		tickerDetails: make(map[string]TickerDetails),
		orderBooks:    make(map[string]OrderBook),
		marketPairs:   make(map[string]string),
	}
}

// StartBackgroundRefresh starts periodic data refresh
func (c *CryptoTracker) startBackgroundRefresh() {
	c.isRunning = true
	go func() {
		for c.isRunning {
			c.refreshTickerData()
			time.Sleep(5 * time.Second)
		}
	}()
}

// StopBackgroundRefresh stops periodic data refresh
func (c *CryptoTracker) stopBackgroundRefresh() {
	c.isRunning = false
}

// RefreshMarketData fetches market details
func (c *CryptoTracker) refreshMarketData() {
	url := config.APIBaseURL + "/exchange/v1/markets_details"
	response, err := c.httpClient.performRequest(url)
	if err != nil {
		fmt.Println("Error fetching market data:", err)
		return
	}

	var markets []MarketDetails
	err = json.Unmarshal([]byte(response), &markets)
	if err != nil {
		fmt.Println("Error parsing market data:", err)
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	for _, market := range markets {
		c.marketDetails[market.CoindcxName] = market
		c.marketPairs[market.CoindcxName] = market.Pair
	}
}

// RefreshTickerData fetches ticker details
func (c *CryptoTracker) refreshTickerData() {
	url := config.APIBaseURL + "/exchange/ticker"
	response, err := c.httpClient.performRequest(url)
	if err != nil {
		fmt.Println("Error fetching ticker data:", err)
		return
	}

	var tickers []TickerDetails
	err = json.Unmarshal([]byte(response), &tickers)
	if err != nil {
		fmt.Println("Error parsing ticker data:", err)
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	for _, ticker := range tickers {
		c.tickerDetails[ticker.Market] = ticker
	}
}

// CryptoAPIServer serves API requests
type CryptoAPIServer struct {
	tracker *CryptoTracker
}

func (s *CryptoAPIServer) start() {
	mux := http.NewServeMux()

	mux.HandleFunc("/livedata", s.handleLiveData)
	mux.HandleFunc("/pairs", s.handlePairs)
	mux.HandleFunc("/ticker", s.handleTicker)

	// Wrap with CORS middleware
	handler := enableCORS(mux)

	address := fmt.Sprintf("%s:%d", config.Host, config.Port)
	fmt.Println("Server starting on", address)

	go func() {
		if err := http.ListenAndServe(address, handler); err != nil {
			fmt.Println("Server error:", err)
		}
	}()
}

func (s *CryptoAPIServer) handleLiveData(w http.ResponseWriter, r *http.Request) {
	// Parse form data if the request is POST
	if r.Method == http.MethodPost {
		err := r.ParseForm()
		if err != nil {
			http.Error(w, "Failed to parse form data", http.StatusBadRequest)
			return
		}
	}

	// Check both query parameters and form data for the 'symbol' parameter
	market := r.URL.Query().Get("symbol")
	if market == "" {
		market = r.FormValue("symbol") // Check the form data
	}

	if market == "" {
		http.Error(w, "Missing 'symbol' parameter", http.StatusBadRequest)
		return
	}

	response := s.tracker.handleDataRequest(market)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// HandleDataRequest processes market data requests
func (c *CryptoTracker) handleDataRequest(marketName string) map[string]interface{} {
	response := make(map[string]interface{})
	if pair, exists := c.marketPairs[marketName]; exists {
		c.refreshOrderBook(pair)
		response["pair"] = marketName
		response["order_book"] = c.orderBooks[pair]
	}
	return response
}
// RefreshOrderBook fetches order book details
func (c *CryptoTracker) refreshOrderBook(pair string) {
	url := "https://public.coindcx.com/market_data/orderbook?pair=" + pair
	response, err := c.httpClient.performRequest(url)
	if err != nil {
		fmt.Println("Error fetching order book data:", err)
		return
	}
	var orderBook OrderBook
	err = json.Unmarshal([]byte(response), &orderBook)
	if err != nil {
		fmt.Println("Error parsing order book data:", err)
		return
	}
	c.orderBooks[pair] = orderBook
}
func (s *CryptoAPIServer) handlePairs(w http.ResponseWriter, r *http.Request) {
	pairs := []string{}
	s.tracker.mutex.RLock()
	for pair := range s.tracker.marketPairs {
		pairs = append(pairs, pair)
	}
	s.tracker.mutex.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string][]string{"pairs": pairs})
}

func (s *CryptoAPIServer) handleTicker(w http.ResponseWriter, r *http.Request) {
	tickers := []TickerDetails{}
	s.tracker.mutex.RLock()
	for _, ticker := range s.tracker.tickerDetails {
		tickers = append(tickers, ticker)
	}
	s.tracker.mutex.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(tickers)
}

func enableCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func main() {
	err := loadConfig("config.json")
	if err != nil {
		fmt.Println("Failed to load configuration:", err)
		os.Exit(1)
	}

	tracker := newCryptoTracker()
	tracker.refreshMarketData()
	tracker.startBackgroundRefresh()

	// Handle graceful shutdown
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)

	server := CryptoAPIServer{tracker: tracker}
	server.start()

	<-stop
	fmt.Println("\nShutting down server...")
	tracker.stopBackgroundRefresh()
	fmt.Println("Server gracefully stopped.")
}
