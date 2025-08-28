/**
 * @file fill_reconciliation.cpp
 * @brief Implementation of fill reconciliation between expected and actual executions
 * 
 * @details 
 * Reconciles expected fills with actual fills reported by exchanges,
 * detects discrepancies, and triggers correction processes.
 * 
 * Created on: May 5, 2025
 */

 #include "execution/fill_reconciliation.hpp"
 #include <algorithm>
 #include <cmath>
 #include <sstream>
 
 namespace trading {
 namespace execution {
 
 std::string FillDiscrepancy::toString() const {
     std::stringstream ss;
     ss << "Discrepancy detected for order " << order_id << " on " << exchangeId.toString() << "\n";
     ss << "  Trading pair: " << tradingPair << "\n";
     ss << "  Type: " << discrepancy_type << "\n";
     
     if (discrepancy_type == "PRICE") {
         ss << "  Expected price: " << expected_price << ", Actual price: " << actual_price << "\n";
         ss << "  Quantity: " << expected_quantity << "\n";
     } 
     else if (discrepancy_type == "QUANTITY") {
         ss << "  Expected quantity: " << expected_quantity << ", Actual quantity: " << actual_quantity << "\n";
         ss << "  Price: " << expected_price << "\n";
     }
     else if (discrepancy_type == "MISSING") {
         ss << "  Expected fill not received (quantity: " << expected_quantity << ", price: " << expected_price << ")\n";
     }
     else if (discrepancy_type == "EXTRA") {
         ss << "  Unexpected fill received (quantity: " << actual_quantity << ", price: " << actual_price << ")\n";
     }
     
     auto time_t = std::chrono::system_clock::to_time_t(detectionTime);
     ss << "  Detected at: " << std::ctime(&time_t);
     
     return ss.str();
 }
 
 FillReconciliation::FillReconciliation() {
     // Nothing to initialize
 }
 bool FillReconciliation::initialize() {
     // Initialization logic if needed
     return true;
 }
 void FillReconciliation::start() {
     // Start any necessary threads or processes
        // For now, we just ensure the mutex is initialized
    std::lock_guard<std::mutex> lock(mutex_);
 }
 void FillReconciliation::stop() {
     // Stop any running threads or processes

 }

 void FillReconciliation::recordExpectedFill(const std::string& order_id, const models::Fill& expected_fill) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Create a fill record
     FillRecord record;
     record.fill = expected_fill;
     record.reconciled = false;
     record.timestamp = std::chrono::system_clock::now();
     
     // Add to expected fills
     expected_fills_[order_id].push_back(record);
 }
 
 bool FillReconciliation::recordActualFill(const std::string& order_id, const models::Fill& actual_fill) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Create a fill record
     FillRecord record;
     record.fill = actual_fill;
     record.reconciled = false;
     record.timestamp = std::chrono::system_clock::now();
     
     // Add to actual fills
     actual_fills_[order_id].push_back(record);
     
     // Try to reconcile with expected fills
     bool reconciled = false;
     
     // Price and quantity tolerance (0.1% for price, 0.001 for quantity as examples)
     double price_tolerance = 0.001;    // 0.1%
     double quantity_tolerance = 0.001; // Tiny amount of crypto
     
     // Find a matching expected fill
     auto& expected = expected_fills_[order_id];
     for (auto& exp_record : expected) {
         if (!exp_record.reconciled && matchFills(exp_record.fill, actual_fill, price_tolerance, quantity_tolerance)) {
             // Found a match
             exp_record.reconciled = true;
             record.reconciled = true;
             
             // Add to reconciled fills
             reconciled_fills_[order_id].push_back(actual_fill);
             
             reconciled = true;
             break;
         }
     }
     
     // If no match was found, check for discrepancies
     if (!reconciled) {
         FillDiscrepancy discrepancy;
         discrepancy.order_id = order_id;
         discrepancy.exchangeId = actual_fill.exchangeId;
         discrepancy.tradingPair = "";  // We don't have this info in the Fill struct
         discrepancy.actual_quantity = actual_fill.quantity;
         discrepancy.actual_price = actual_fill.price;
         discrepancy.expected_quantity = 0.0;  // Will be filled if we find a close match
         discrepancy.expected_price = 0.0;     // Will be filled if we find a close match
         discrepancy.detectionTime = std::chrono::system_clock::now();
         
         // Try to find a close match to determine the type of discrepancy
         bool close_match_found = false;
         for (const auto& exp_record : expected) {
             if (!exp_record.reconciled) {
                 // Check if prices match but quantities don't
                 if (std::abs(exp_record.fill.price - actual_fill.price) / exp_record.fill.price < price_tolerance) {
                     discrepancy.discrepancy_type = "QUANTITY";
                     discrepancy.expected_quantity = exp_record.fill.quantity;
                     discrepancy.expected_price = exp_record.fill.price;
                     close_match_found = true;
                     break;
                 }
                 
                 // Check if quantities match but prices don't
                 if (std::abs(exp_record.fill.quantity - actual_fill.quantity) < quantity_tolerance) {
                     discrepancy.discrepancy_type = "PRICE";
                     discrepancy.expected_quantity = exp_record.fill.quantity;
                     discrepancy.expected_price = exp_record.fill.price;
                     close_match_found = true;
                     break;
                 }
             }
         }
         
         // If no close match, it's an extra fill
         if (!close_match_found) {
             discrepancy.discrepancy_type = "EXTRA";
         }
         
         // Notify about the discrepancy
         notifyDiscrepancy(discrepancy);
     }
     
     return reconciled;
 }
 
 std::vector<FillDiscrepancy> FillReconciliation::checkForMissingFills(int64_t timeout_ms) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<FillDiscrepancy> discrepancies;
     auto now = std::chrono::system_clock::now();
     
     // Check all expected fills
     for (const auto& pair : expected_fills_) {
         std::string order_id = pair.first;
         
         for (const auto& exp_record : pair.second) {
             // Skip reconciled fills
             if (exp_record.reconciled) {
                 continue;
             }
             
             // Check if the fill has timed out
             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                 now - exp_record.timestamp).count();
                 
             if (elapsed > timeout_ms) {
                 // Create a discrepancy
                 FillDiscrepancy discrepancy;
                 discrepancy.order_id = order_id;
                 discrepancy.exchangeId = exp_record.fill.exchangeId;
                 discrepancy.tradingPair = "";  // We don't have this info
                 discrepancy.expected_quantity = exp_record.fill.quantity;
                 discrepancy.expected_price = exp_record.fill.price;
                 discrepancy.actual_quantity = 0.0;
                 discrepancy.actual_price = 0.0;
                 discrepancy.discrepancy_type = "MISSING";
                 discrepancy.detectionTime = now;
                 
                 discrepancies.push_back(discrepancy);
                 
                 // Notify about the discrepancy
                 notifyDiscrepancy(discrepancy);
             }
         }
     }
     
     return discrepancies;
 }
 
 std::vector<FillDiscrepancy> FillReconciliation::checkForExtraFills() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<FillDiscrepancy> discrepancies;
     
     // Check all actual fills
     for (const auto& pair : actual_fills_) {
         std::string order_id = pair.first;
         
         for (const auto& act_record : pair.second) {
             // Skip reconciled fills
             if (act_record.reconciled) {
                 continue;
             }
             
             // Create a discrepancy
             FillDiscrepancy discrepancy;
             discrepancy.order_id = order_id;
             discrepancy.exchangeId = act_record.fill.exchangeId;
             discrepancy.tradingPair = "";  // We don't have this info
             discrepancy.expected_quantity = 0.0;
             discrepancy.expected_price = 0.0;
             discrepancy.actual_quantity = act_record.fill.quantity;
             discrepancy.actual_price = act_record.fill.price;
             discrepancy.discrepancy_type = "EXTRA";
             discrepancy.detectionTime = std::chrono::system_clock::now();
             
             discrepancies.push_back(discrepancy);
             
             // Notify about the discrepancy
             notifyDiscrepancy(discrepancy);
         }
     }
     
     return discrepancies;
 }
 
 void FillReconciliation::registerDiscrepancyCallback(DiscrepancyCallback callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     discrepancy_callbacks_.push_back(callback);
 }
 
 std::vector<models::Fill> FillReconciliation::getReconciledFills(const std::string& order_id) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     auto it = reconciled_fills_.find(order_id);
     if (it != reconciled_fills_.end()) {
         return it->second;
     }
     
     return {};  // No reconciled fills
 }
 
 std::vector<models::Fill> FillReconciliation::getUnreconciledExpectedFills(const std::string& order_id) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<models::Fill> unrec_fills;
     
     auto it = expected_fills_.find(order_id);
     if (it != expected_fills_.end()) {
         for (const auto& record : it->second) {
             if (!record.reconciled) {
                 unrec_fills.push_back(record.fill);
             }
         }
     }
     
     return unrec_fills;
 }
 
 std::vector<models::Fill> FillReconciliation::getUnreconciledActualFills(const std::string& order_id) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<models::Fill> unrec_fills;
     
     auto it = actual_fills_.find(order_id);
     if (it != actual_fills_.end()) {
         for (const auto& record : it->second) {
             if (!record.reconciled) {
                 unrec_fills.push_back(record.fill);
             }
         }
     }
     
     return unrec_fills;
 }
 
 int FillReconciliation::purgeOldReconciliationData(int64_t age_threshold_ms) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     int purged_count = 0;
     auto now = std::chrono::system_clock::now();
     std::vector<std::string> empty_records;
     
     // Helper to purge old records from a map of records
     auto purge_records = [&](auto& records_map) {
         for (auto& pair : records_map) {
             auto& records = pair.second;
             
             // Remove old records
             size_t original_size = records.size();
             records.erase(
                 std::remove_if(records.begin(), records.end(), 
                     [&](const FillRecord& record) {
                         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - record.timestamp).count();
                         return elapsed > age_threshold_ms;
                     }
                 ),
                 records.end()
             );
             
             // Count purged records
             purged_count += (original_size - records.size());
             
             // Track empty records for removal
             if (records.empty()) {
                 empty_records.push_back(pair.first);
             }
         }
         
         // Remove empty entries
         for (const auto& key : empty_records) {
             records_map.erase(key);
         }
         empty_records.clear();
     };
     
     // Purge from all three maps
     purge_records(expected_fills_);
     purge_records(actual_fills_);
     
     // Also clean up reconciled_fills_ for consistency
     for (auto it = reconciled_fills_.begin(); it != reconciled_fills_.end();) {
         bool has_expected = expected_fills_.find(it->first) != expected_fills_.end();
         bool has_actual = actual_fills_.find(it->first) != actual_fills_.end();
         
         if (!has_expected && !has_actual) {
             it = reconciled_fills_.erase(it);
         } else {
             ++it;
         }
     }
     
     return purged_count;
 }
 
 void FillReconciliation::notifyDiscrepancy(const FillDiscrepancy& discrepancy) {
     // Call all registered callbacks
     for (const auto& callback : discrepancy_callbacks_) {
         callback(discrepancy);
     }
 }
 
 bool FillReconciliation::matchFills(const models::Fill& expected, const models::Fill& actual, 
                                  double price_tolerance, double quantity_tolerance) {
     // Check for exact match on fill ID if available
     if (!expected.fill_id.empty() && !actual.fill_id.empty() && 
         expected.fill_id == actual.fill_id) {
         return true;
     }
     
     // Check prices and quantities with tolerance
     bool price_match = std::abs(expected.price - actual.price) / expected.price < price_tolerance;
     bool quantity_match = std::abs(expected.quantity - actual.quantity) < quantity_tolerance;
     
     return price_match && quantity_match;
 }
 
 } // namespace execution
 } // namespace trading