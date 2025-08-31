private:
    std::mutex orderBookMutex_;
    std::atomic<bool> syncInProgress_{false};
    
    bool validateAndUpdateAtomic(const OrderBookUpdate& update);
    void triggerResync(const ExchangeId& exchange, const std::string& symbol);
