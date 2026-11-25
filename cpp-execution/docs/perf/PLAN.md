# Plan 5 ngày: Audit & tối ưu latency hot path

## Mục tiêu

Dùng project này làm portfolio khi apply vị trí HFT / low-latency C++.
Thứ nhà tuyển dụng muốn thấy không phải là "hệ thống có nhiều module", mà là:

- Hiểu rõ data flow, ownership, threading của hot path.
- Biết **đo** đúng cách: p50/p99/p99.9, không chỉ trung bình; môi trường đo có kiểm soát.
- Dùng `perf` để tìm bottleneck thật (cache miss, allocation, lock), không đoán.
- Có lưới an toàn correctness (deterministic replay, sanitizer) trước khi tối ưu.
- Kết quả before/after **tái hiện được bằng một lệnh**, kèm giải thích trade-off.

Sản phẩm cuối: `docs/perf/README.md` + `benchmarks/` + `tests/replay/` — đủ để đưa link
vào CV và trình bày trong 10–15 phút phỏng vấn.

## Phạm vi

Đo và tối ưu **đường tick-to-signal** của `semi_hft` với Binance `@depth20@100ms` (27 cặp):

```
WS frame → BinanceWebSocketClient → Q1 → ExchangeManager::processingWorker (parse, normalize)
        → OrderBookReconstructor / OrderBook → Q2 → orderbook worker → Q3 → StrategyEngine
        → Q4 → ImbalanceDetectionEngine workers → signal → Q5 → RiskManager → Q6 → Execution
```

Ngoài phạm vi: Python strategy, monitoring, training, DR/admin.

## Hiện trạng

Xem [01_audit.md](01_audit.md). Tóm tắt: có 6 lần chuyển thread, queue đầu tiên dùng polling
`sleep_for(100ms)`, mỗi update bị xử lý 2 lần, parse 2 lần, và có data race ở strategy khi chạy
2 worker.

---

## Ngày 1 — Audit data flow, ownership, concurrency

**Việc làm**
1. Vẽ data flow hot path: mỗi stage chạy trên thread nào, giao tiếp qua gì
   (mutex queue / ZMQ / gọi trực tiếp), copy hay move ở đâu.
2. Bảng ownership cho object chính (`OrderBook`, `OrderBookDelta`, message buffer, `Order`):
   ai tạo, ai giữ, ai giải phóng; `shared_ptr` có thật sự cần không.
3. Bảng concurrency: mọi mutex/atomic trên hot path → bảo vệ dữ liệu gì, ai đọc/ghi,
   có giữ lock khi gọi callback / I/O không.
4. Đánh dấu nghi vấn:
   - key `double` trong `std::map` (so sánh float + node-based → cache miss)
   - copy cả map `previous_bids_/asks_` mỗi update
   - `std::vector` cấp phát mới trong mỗi `OrderBookDelta`
   - callback `std::function` gọi khi đang giữ lock
   - compress/serialize trên hot thread
   - dùng `system_clock` thay vì clock monotonic khi đo

**Deliverable:** `docs/perf/01_audit.md` (sơ đồ + 2 bảng + danh sách giả thuyết xếp hạng).

**Done khi:** mỗi giả thuyết có cách đo cụ thể ở ngày 2/3.

---

## Ngày 2 — Benchmark latency p50/p99/p99.9 + throughput

**Việc làm**
1. **Thu feed trước:** ghi ~1–2 triệu message thật (Binance depth + trade) ra file nhị phân.
   Dùng chung cho benchmark (ngày 2) và replay (ngày 4).
2. Thêm `benchmarks/` (Google Benchmark), option CMake `BUILD_BENCHMARKS`:
   - Micro: `applySnapshot`, `applyUpdate`, `getBids(depth)`, tính imbalance, pre-trade check.
   - Macro: replay file feed qua toàn pipeline in-process (không mạng).
3. Đo latency per-stage bằng `rdtsc` / `steady_clock` → HdrHistogram → p50/p99/p99.9/max.
4. Đo throughput (msg/s) ở chế độ replay tối đa.
5. Chuẩn hóa môi trường: Release `-O3`, `taskset` pin CPU, governor `performance`,
   tắt turbo nếu được, chạy 5 lần lấy median.

**Deliverable:** `benchmarks/`, `scripts/bench.sh`, `results/baseline.json` + `baseline.md`.

**Done khi:** chạy lại 2 lần lệch < 5% ở p50, < 15% ở p99.

---

## Ngày 3 — Allocation, cache miss, lock contention bằng `perf`

Build `RelWithDebInfo` + `-fno-omit-frame-pointer`.

| Vấn đề | Công cụ / lệnh |
|---|---|
| CPU hotspot | `perf record -g --call-graph dwarf` + flamegraph |
| Cache miss | `perf stat -e cache-misses,L1-dcache-load-misses,LLC-load-misses,branch-misses` |
| Cache miss theo dòng code | `perf mem` / `perf record -e mem_load_retired.l3_miss`; `perf c2c` cho false sharing |
| Allocation | `perf probe` uprobe trên `malloc`/`operator new`, hoặc hook đếm `new` trong benchmark; `heaptrack` |
| Lock contention | `perf lock record/report` (hoặc `perf trace -s` xem futex), off-CPU flamegraph |
| Context switch | `perf stat -e context-switches,cpu-migrations` |

**Deliverable:** `docs/perf/03_profile.md` — flamegraph, alloc/msg, cache-miss/msg,
lock wait time; mỗi giả thuyết ngày 1 ghi rõ xác nhận / bác bỏ.

**Done khi:** có top 3 bottleneck **đã đo**, có số liệu.

---

## Ngày 4 — Correctness: deterministic replay, sanitizer, stress test

Làm **trước** khi sửa code ngày 5 để có lưới an toàn.

**Việc làm**
1. **Deterministic replay:** feed ngày 2 → pipeline với clock giả (inject time source).
   Hash trạng thái book + signal/order sinh ra mỗi bước → golden output trong
   `tests/replay/golden/`. Pass khi hash khớp bit-by-bit.
2. **Sanitizer:** dùng `USE_SANITIZERS` sẵn có (ASan+UBSan), thêm option TSan riêng.
   Chạy toàn bộ `ctest` + replay test.
3. **Stress test:** multi-producer/consumer trên publisher và OrderBook, burst lớn,
   queue đầy (back-pressure), reconnect, snapshot xen giữa delta — chạy dưới TSan.
4. Test so sánh giá `double` với tick size lẻ.

**Deliverable:** replay test trong ctest, CMake preset `asan`/`tsan`, danh sách bug tìm được.

**Done khi:** 0 lỗi sanitizer, replay ổn định 3 lần liên tiếp.

---

## Ngày 5 — Tối ưu bottleneck đã đo, README, before/after

**Việc làm**
1. Chỉ sửa top 3 từ ngày 3. Mỗi thay đổi: replay test pass → benchmark lại → commit riêng.
   Ứng viên (tùy số đo):
   - key giá → `int64` ticks; `std::map` → flat array / sorted vector
   - bỏ copy `previous_*`, sinh delta trong lúc apply
   - mutex queue của publisher → SPSC ring buffer; compress ra khỏi hot thread
   - reuse buffer cho delta (pool / `reserve`), gọi callback ngoài lock
2. Bảng before/after: p50/p99/p99.9, throughput, alloc/msg, cache-miss/msg, lock wait + chart.
3. `docs/perf/README.md`: môi trường (CPU, kernel, compiler, flags), cách tái hiện
   (`scripts/bench.sh`), kết quả, **những gì đã thử mà không cải thiện** và vì sao.

**Done khi:** mọi thay đổi pass replay + sanitizer; mọi con số công bố tái hiện được bằng một lệnh.

---

## Rủi ro & lưu ý

- **Ngày 2 nặng nhất** (recorder + harness + histogram). Nếu trễ, dời macro benchmark sang sáng ngày 3.
- Thu feed đầu ngày 2 — cả benchmark lẫn replay phụ thuộc vào nó.
- Không tối ưu thứ gì chưa xuất hiện trong profile ngày 3, dù nhìn có vẻ chậm.
- Khi trình bày phỏng vấn: ưu tiên kể **quy trình đo → giả thuyết → kiểm chứng → trade-off**,
  hơn là chỉ khoe con số cải thiện.
