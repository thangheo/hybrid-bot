# Ngày 1: Audit data flow, ownership, concurrency (hybrid-bot)

Phạm vi: đường tick-to-signal của `semi_hft` (`Trading_system`) với Binance `@depth20@100ms`
(27 cặp từ `config/strategy_config.json`, `trading_system.cpp:947`). Tham chiếu theo dạng
`file:line` tính từ `cpp-execution/`, tại commit `101d376`.

Mức độ tin cậy: **[code]** = đọc code, có trích dẫn; **[đo]** = sẽ đo ở ngày 2/3.

## TL;DR

1. **Một tick đi qua 6 lần chuyển thread** và 6 hàng đợi trước khi ra tới signal:
   io → processing → orderbook worker → strategy workers → detector pool → signal worker.
2. **Hàng đợi đầu tiên dùng polling `sleep_for(100ms)`** (`exchange_manager.cpp:532`). Ở tốc độ
   feed thật (khoảng 270 msg/s cho 27 cặp), message đến lúc worker đang ngủ phải chờ tới 100 ms.
   Đây gần như chắc chắn là nguồn latency lớn nhất. **[code]**, sẽ được **[đo]**.
3. **Độ trễ này bị che khỏi số đo**: `receivedTimestamp` được gán lại bằng `now()` lúc parse,
   tức là *sau* khi đã chờ trong queue (`binance_adapter.cpp:473`). **[code]**
4. **Mỗi update bị xử lý 2 lần**: `processingWorker` gọi `normalizer_->processOrderBookUpdate()`,
   hàm này gọi callback, rồi lại gọi `normalizedCallback_()` trực tiếp (`exchange_manager.cpp:504-509`).
   Hậu quả: reconstructor apply lại snapshot, publish thêm, và đẩy vào queue strategy 2 lần. **[code]**
5. **Parse 2 lần + serialize 1 lần**: client parse DOM, `dump()` phần `data`, rồi adapter parse
   lại bằng SAX. **[code]**
6. **Data race ở strategy** khi chạy 2 worker (`strategyOrderbookWorkerCount_ = 2`): các biến
   `static` dùng chung, iterator dùng sau khi đã nhả lock, và cùng một `OrderBook` đang bị sửa
   được đọc song song. **[code]**, TSan sẽ xác nhận ở ngày 4.

Điểm tốt so với repo `hft` cũ: các deadlock do gọi callback khi giữ lock **đã được sửa**
(`order_book.cpp:174`, có comment "FIXED"), và adapter dùng SAX parser.

---

## 1. Data flow và các lần chuyển thread

```
T0  [Boost.Asio io thread]  BinanceWebSocketClient::startRead
     ├─ buffers_to_string                    COPY            binance_websocket_client.cpp:524
     ├─ LOG_DEBUG("..."+to_string(len))      cấp phát dù tắt debug                     :540
     ├─ json::parse (DOM)                    PARSE #1                                    :542
     ├─ lock stats_mutex_                                                                :546
     ├─ auto stream_data = data["data"]      COPY cây JSON con                           :1021
     ├─ stream_data.dump() + shrink_to_fit   SERIALIZE + realloc                         :1028
     └─ lock callback_mutex_ → ExchangeManager::onWebSocketRawData
          └─ lock enqueueMutex_; std::move(const&) = COPY payload    exchange_manager.cpp:172-178
             ▼  moodycamel::ReaderWriterQueue (SPSC)                                    Q1
T1  [ExchangeManager::processingWorker]                                    exchange_manager.cpp:480
     ├─ queue rỗng → sleep_for(100ms)        ⚠ POLLING                                   :532
     ├─ BinanceAdapter::parseOrderBookData   PARSE #2 (SAX), stod mỗi level, checksum
     │      receivedTimestamp = now()        ⚠ che thời gian chờ ở Q1       binance_adapter.cpp:473
     ├─ normalizer_->processOrderBookUpdate  lock normalizer mutex_, callback ─┐  (lần 1)
     └─ normalizedCallback_(normalized)                                       ─┤  (lần 2)
          DataIngestionService::onNormalizedOrderBook  ◄───────────────────────┘
           ├─ reconstructor_->processSnapshot   copy bids/asks       reconstructor.cpp:273
           │    └─ OrderBookManager::processUpdate → OrderBook::applySnapshot
           │         captureOrderBookState: copy 2 std::map (chỉ dùng để tính dung lượng bộ nhớ)
           │         delta "xóa hết + thêm hết" ~80 push_back, copy vào deque
           │    └─ publishOrderBook: getSnapshot() copy CẢ BOOK, TRƯỚC khi throttle 50ms;
           │         static unordered_map không có lock; protobuf → ZMQ   reconstructor.cpp:438-461
           └─ orderBookCallback_(update)   lambda nhận `auto update` THEO GIÁ TRỊ → copy
                lock orderbookQueueMutex_, push deque, notify             trading_system.cpp:2153
             ▼  std::deque + mutex + condvar                                              Q2
T2  [orderbookWorkerLoop ×1]   chỉ tra getOrderBook() rồi đẩy tiếp   trading_system.cpp:1466
     └─ push shared_ptr<OrderBook> (book ĐANG SỐNG, không phải snapshot)
             ▼  std::deque + mutex + condvar                                              Q3
T3  [strategyOrderbookWorkerLoop ×2]                                    trading_system.cpp:1526
     └─ StrategyEngine::processOrderBook(*book)                     strategy_engine.cpp:302
          OrderBookAnalyzer, getOrderBookDelta, VolumeProfile (POC, value area, anomalies, skew),
          MomentumCorrelation, rồi imbalanceDetector_->analyzeMetrics(metrics)
          └─ copy OrderBookMetrics vào queue + copy vào latestSnapshots_ (lock lồng nhau)
             ▼  std::deque + mutex + condvar                                              Q4
T4  [detector workers × hardware_concurrency() = 20]     imbalance_detection_engine.cpp:765
     └─ khoảng 10 detector → signal → StrategyEngine signal callback
             ▼  std::deque + mutex + condvar                                              Q5
T5  [signalWorkerLoop ×1] dedup → std::cout → RiskManager::validateSignal → std::cout
                                                                  trading_system.cpp:1601-1703
             ▼  exec queue                                                                Q6
T6  [execWorkerLoop × hw/2] → ExecutionService::executeSignal
```

## 2. Ownership

| Object | Sinh ra ở | Đi qua | Vấn đề |
|---|---|---|---|
| Frame WS | `flat_buffer` (io) | → `std::string` → DOM → `dump()` string | 3 bản biểu diễn của cùng một dữ liệu |
| `RawMarketData` | io thread | Q1 | `std::move` trên `const&` thực chất là copy |
| `OrderBookUpdate` | processing thread | callback ×2 → reconstructor (copy) → lambda (copy) → Q2 | ít nhất 3 lần copy 2 vector mỗi lần đi qua |
| `OrderBook` | `OrderBookManager` (`shared_ptr`) | Q3 (theo `shared_ptr`) | strategy đọc book **trong lúc** processing thread vẫn đang sửa nó. Kết quả không nhất quán theo thời điểm, chỉ được che bởi mutex bên trong mỗi getter |
| `OrderBookMetrics` (map + vector) | strategy | Q4 (copy) + `latestSnapshots_` (copy) | struct lớn, copy 2 lần mỗi tick |

## 3. Concurrency

| Vị trí | Vấn đề | Hậu quả |
|---|---|---|
| `processingWorker` `sleep_for(100ms)` (`exchange_manager.cpp:532`) | dùng polling thay vì chờ được báo thức | tới **100 ms** chờ thêm mỗi message (H1) |
| Q2, Q3, Q4, Q5: `std::deque` + mutex + `condition_variable` | mỗi lần chuyển cần 1 lần lock + notify; nếu consumer đang ngủ thì tốn futex syscall và chi phí đánh thức | vài µs tới hàng chục µs mỗi lần chuyển, có 5 lần như vậy (H2) |
| Q2→Q3: `orderbookWorkerLoop` | stage không làm việc gì ngoài chuyển tiếp | 1 lần chuyển thread thừa |
| Q3 có 2 consumer, Q4 có 20 consumer | **không giữ thứ tự theo symbol**: hai tick của cùng một symbol có thể được xử lý ngược thứ tự. Detector có trạng thái (`lastMetrics_`, trend) nên sẽ thấy dữ liệu sai thứ tự | sai về logic, không chỉ là chậm |
| Q3 chứa `shared_ptr` tới book đang sống, cộng với lỗi xử lý 2 lần (B1) | cùng một book được đưa vào queue nhiều lần; strategy xử lý **trạng thái mới nhất** nhiều lần | lãng phí CPU |
| `strategy_engine.cpp:442` `static size_t updateCount` | `++` từ 2 thread mà không phải atomic | data race, UB |
| `strategy_engine.cpp:452` `static unordered_map lastAllowedMetricTime` | ghi từ 2 thread mà không có lock | data race; map rehash giữa chừng có thể crash |
| `strategy_engine.cpp:353-381` `vpIt` | iterator lấy trong lock nhưng dùng sau khi nhả lock, trong khi thread khác có thể insert vào map | iterator không còn hợp lệ, UB |
| `reconstructor.cpp:451` `static unordered_map lastPublishTime` | không có lock | data race nếu reconstructor được gọi từ nhiều thread |
| Normalizer giữ `mutex_` trong suốt callback (`order_book_normalizer.cpp:43-46`) | lock bao trùm cả reconstructor, publish ZMQ và đẩy vào Q2 | đường xử lý bị tuần tự hóa; dễ deadlock nếu sau này có code gọi ngược lại vào normalizer |
| Signal worker | `std::cout << ... << std::endl` 2 lần cho mỗi signal, cộng với in các target | syscall nằm trên đường đi tới risk |

## 4. Bug correctness

| # | Bug | Bằng chứng | Mức độ |
|---|---|---|---|
| B1 | Mỗi update đi vào `onNormalizedOrderBook` **2 lần** | `exchange_manager.cpp:504-509` + `order_book_normalizer.cpp:42-47` | xử lý gấp đôi; snapshot có cùng sequence vẫn được apply (`order_book.cpp:102` chỉ bỏ qua khi `<`) |
| B2 | Latency trong Q1 bị che vì `receivedTimestamp` gán lại lúc parse | `binance_adapter.cpp:473`; trong khi `rawData.timestamp` đã có từ io thread (`:1027`) | mọi chỉ số latency nội bộ đều báo thấp hơn thực tế |
| B3 | Strategy chạy 2 worker không giữ thứ tự theo symbol; detector 20 worker cũng vậy | §3 | detector có trạng thái nhận dữ liệu sai thứ tự |
| B4 | Các data race ở §3 | §3 | UB; sẽ được TSan xác nhận ở ngày 4 |
| B5 | `publishOrderBook` copy cả book trước khi throttle; comment ghi "2/s, 100 ms" nhưng code là 50 ms | `reconstructor.cpp:449-457` | lãng phí, và comment lệch so với code |

## 5. Giả thuyết bottleneck và cách đo

| # | Giả thuyết | Ước lượng | Đo bằng gì |
|---|---|---|---|
| H1 | `sleep_for(100ms)` ở Q1 | **0–100 ms mỗi message**, lớn hơn mọi chi phí CPU khác nhiều bậc | replay theo đúng nhịp thời gian thật, đo từ `rawData.timestamp` tới khi ra khỏi Q1 |
| H2 | 5 lần chuyển qua queue có mutex+condvar | khoảng 5–50 µs mỗi lần (đánh thức thread, futex) | timestamp ở mỗi lần đưa vào và lấy ra khỏi queue; `perf sched`, đếm context switch |
| H3 | Parse 2 lần + `dump` + khoảng 6 lần copy payload | vài chục µs mỗi message với 20 level × 2 bên | microbench chạy 1 thread cho riêng phần client + adapter |
| H4 | Xử lý 2 lần (B1) | gấp đôi chi phí của toàn bộ phần sau Q1 | đếm số lần gọi `onNormalizedOrderBook` trên mỗi message |
| H5 | `applySnapshot`: copy `previous_*`, delta khoảng 80 phần tử, `std::map` dạng node | khoảng 100+ lần cấp phát mỗi snapshot | đếm cấp phát mỗi message; `perf stat` cache-miss |
| H6 | `publishOrderBook` gọi `getSnapshot()` trước khi throttle; protobuf + ZMQ | 1 lần copy cả book mỗi message | microbench |
| H7 | Strategy tính lại volume profile (POC, value area, anomalies, skew) ở **mọi** tick | chưa rõ; nhiều khả năng là phần nặng nhất về CPU | flamegraph |
| H8 | `OrderBookMetrics` copy 2 lần vào Q4 và `latestSnapshots_` | vài chục lần cấp phát | đếm cấp phát |

## 6. Quyết định cho ngày 2

Hệ thống này **có nối đầy đủ từ đầu đến cuối** (khác `hft`), nên ngày 2 sẽ đo theo 2 hướng:

1. **End-to-end, chạy đúng các thread thật**: replay feed `@depth20@100ms` thu từ Binance
   (`scripts/record_binance.py`) vào `ExchangeManager::onWebSocketRawData` **theo đúng nhịp thời
   gian đã ghi**. Đo latency từ lúc nhận (`rawData.timestamp`) tới khi ra khỏi từng queue và tới
   lúc có signal. Cách này phơi bày được H1, H2 và H4.
2. **Chi phí CPU từng stage, chạy 1 thread**: parse, reconstructor, strategy. Cách này đo được
   H3, H5, H6, H7 và H8.

Khác với lần làm trên `hft`, **ở đây chưa có bug nào chặn việc đo**, nên mọi bug sẽ được giữ
nguyên trong baseline để có số liệu before/after.
