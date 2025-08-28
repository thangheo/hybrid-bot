// Example usage
auto adapter_factory = std::make_shared<AdapterFactory>();
auto order_book_manager = std::make_shared<OrderBookManager>();
auto quality_control = std::make_shared<DataQualityControl>();
auto time_sync = std::make_shared<TimeSync>();
auto publisher = std::make_shared<OrderBookPublisher>();

// Create pipeline
auto pipeline = std::make_shared<NormalizationPipeline>(
    adapter_factory,
    order_book_manager,
    quality_control,
    time_sync,
    publisher
);

// Start pipeline
pipeline->start();

// Feed data into pipeline
pipeline->push(market_data);

// Get metrics
auto metrics = pipeline->getMetrics();
std::cout << "Processed: " << metrics->get_total_input_count() << std::endl;

// Get status
auto status = pipeline->getStatus();
printPipelineStatus(status);

// Stop pipeline
pipeline->stop();
