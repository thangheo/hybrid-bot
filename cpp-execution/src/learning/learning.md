# 📄 Paper Overview: From Thresholds to Bayesian Adaptive Models in Orderbook Trading

## 1. Vấn đề ban đầu: Thresholds cứng

Trong trading system dựa trên orderbook, signals thường sinh ra từ metrics như:

* Imbalance (cung cầu)
* Spread
* Volume delta
* Depth liquidity

Thông thường, hệ thống dùng **thresholds cứng**:

```cpp
if (imbalance > 0.6) BUY;
else if (imbalance < -0.6) SELL;
```

Nhược điểm:

* Threshold thấp → quá nhiều tín hiệu giả.
* Threshold cao → quá ít tín hiệu, bỏ lỡ cơ hội.

---

## 2. Giải pháp: Scoring thay vì threshold

Thay vì binary rule, ta tính **score liên tục** từ metrics:

$$
score = w_1 \cdot imbalance + w_2 \cdot spreadChange + w_3 \cdot volumeDelta
$$

Score được map thành xác suất giá tăng bằng logistic:

$$
P(up) = \frac{1}{1 + e^{-\alpha \cdot score}}
$$

Trading decision dựa trên **expected value**:

$$
EV = P(up) \cdot payoff_{long} - (1-P(up)) \cdot payoff_{short}
$$

👉 Không còn threshold cứng, mà là đánh giá liên tục.

---

## 3. Online Weight Update (Gradient Descent)

Trọng số $w_i$ ban đầu không biết → cần **update online** theo dữ liệu mới.

Loss (logistic regression):

$$
L = -(y \ln p + (1-y)\ln(1-p))
$$

Gradient update:

$$
w_i \leftarrow w_i + \eta (y - p) x_i
$$

Code mẫu:

```cpp
w1 += lr * (label - p) * imbalance;
w2 += lr * (label - p) * spreadChange;
w3 += lr * (label - p) * volumeDelta;
```

👉 Nhanh, nhẹ, phù hợp hệ thống real-time.

---

## 4. Bayesian Update (Advanced)

Thay vì chỉ một số duy nhất, mỗi trọng số $w$ là một phân phối:

$$
w \sim \mathcal{N}(\mu, \sigma^2)
$$

Sau quan sát $(x,y)$:

$$
\sigma_{post}^2 = \left(\frac{1}{\sigma^2} + \beta x^2 \right)^{-1}, \quad
\mu_{post} = \sigma_{post}^2 \left(\frac{\mu}{\sigma^2} + \beta x y \right)
$$

Ưu điểm:

* Có notion về **uncertainty**.
* Khi uncertainty cao → giảm trade size.
* Khi uncertainty thấp → tự tin hơn.

---

## 5. Multi-Feature Bayesian Model

Mở rộng cho vector metrics:

$$
\Sigma_{post}^{-1} = \Sigma_0^{-1} + \beta x x^\top, \quad
\mu_{post} = \Sigma_{post} \big(\Sigma_0^{-1} \mu_0 + \beta x y\big)
$$

* $\mu$: vector mean của trọng số.
* $\Sigma$: covariance matrix (uncertainty + correlation giữa metrics).

👉 Hệ thống giờ không chỉ có trọng số tốt nhất, mà còn biết được mức độ tin cậy.

---

## 6. Khi nào dùng PnL làm target?

* **Supervised (y = return/direction):**

  * Dự đoán hướng giá hay return trong horizon cố định.
  * PnL chỉ dùng để evaluate.

* **Reinforcement Learning (reward = PnL):**

  * Action = buy/sell/hold.
  * Reward = PnL thật (sau khi trade, có fee & slippage).
  * Policy học để maximize cumulative PnL.

👉 RL advanced hơn, nhưng noisy & khó train ổn định.
👉 Supervised đơn giản, dùng nhiều trong industry.

---

## 7. Tóm tắt pipeline

1. Bắt đầu từ metrics orderbook (imbalance, spread, volume).
2. Thay threshold → score liên tục.
3. Update trọng số bằng online gradient descent (nhẹ) hoặc Bayesian update (advanced, có uncertainty).
4. Dùng xác suất & expected value để quyết định trade.
5. Nếu muốn advanced hơn nữa → chuyển sang reinforcement learning, optimize reward = PnL trực tiếp.

---

📌 **Kết luận:**

* Threshold → quá cứng, dễ nhiễu.
* Scoring + online learning → cân bằng, nhẹ, dễ triển khai.
* Bayesian update → advanced hơn, có notion confidence, giúp quản lý risk tốt.
* RL với PnL reward → đỉnh cao nhưng phức tạp, thường ở giai đoạn R&D.

---

👉 Bạn có muốn mình viết bản này dưới dạng **prompt “nguyên văn”** để bạn chỉ cần copy-paste vào thread mới, hay muốn giữ dạng **paper note** như trên (có công thức + giải thích)?
