# EoRa Pi (ESP32-S3 + SX1262) Power Analysis  --ChatGPT

This document summarizes the power measurements, calculations, and battery-life estimates for the EoRa Pi using **radio.sleep** and **ESP32-S3 deep sleep**.

[EoRa Pi Source](https://ebyteiot.com/products/ebyte-oem-odm-eora-s3-900tb-22dbm-7km-mini-low-power-and-long-distance-sx1262-rf-module-lora-module-915mhz?_pos=2&_sid=06f8201a9&_ss=r)

---

## 1. Measurement Setup

- **Board:** EoRa Pi (ESP32-S3 + SX1262 LoRa)  
- **Power measurement:** Nordic Power Profiler Kit II (PPK II) in series with battery (ammeter mode)  
- **Sampling rate:** 100,000 samples/sec (10 µs/sample)  
- **Battery:** 3000 mAh LiPo pack  

---

## 2. Observed Current Behavior

| Event                     | Current        | Duration     | Notes |
|----------------------------|---------------|------------|-------|
| Power-on / init spike      | 46 mA         | 50 ms      | SX1262 + board initialization. Negligible battery impact. |
| Sleep / radio.sleep        | 25.38 µA      | ~990 ms    | ESP32-S3 deep sleep + radio.sleep baseline. |
| Listen pulse (duty-cycle)  | 6.91 mA       | 9.91 ms    | Radio wakes briefly to listen, repeats every 1 s. |
| 1-second cycle             | Avg ~93.6 µA  | 1 s        | Combined average of pulse + sleep. |
| 14 minutes (~840 cycles)   | Avg ~93.6 µA  | 840 s      | Total average remains ~93.6 µA. |

---

## 3. Duty-Cycle Analysis

- **Duty of listen pulse:**  
\[
D = \frac{t_{RX}}{T} = \frac{9.91\ \text{ms}}{1000\ \text{ms}} \approx 0.00991
\]

- **Average current formula:**  
\[
I_{avg} = D \cdot I_{RX} + (1-D) \cdot I_{sleep}
\]

- **Calculation (digit-by-digit):**  
1. Pulse contribution:  
\[
0.00991 \times 6.91 = 0.0684781\ \text{mA}
\]  
2. Sleep contribution:  
\[
0.99009 \times 0.02538 = 0.02512848\ \text{mA}
\]  
3. Total average:  
\[
I_{avg} = 0.0684781 + 0.02512848 \approx 0.09361\ \text{mA} \approx 93.6\ \mu\text{A}
\]

---

## 4. Energy Consumption Over 14 Minutes

- Total charge:  
\[
Q = I_{avg} \times \frac{t}{3600} = 0.09360658\ \text{mA} \times \frac{840\ \text{s}}{3600} \approx 0.02184\ \text{mAh}
\]

- Observation: **negligible consumption** for 14 minutes.

---

## 5. Init Spike Contribution

- **Spike:** 46 mA for 50 ms  
- Charge consumed:  
\[
Q_\text{spike} = 46\ \text{mA} \times \frac{0.050\ \text{s}}{3600} \approx 0.0006389\ \text{mAh}
\]  
- Percentage of 3000 mAh battery:  
\[
\frac{0.0006389}{3000} \times 100\% \approx 0.0000213\%
\]  

✅ **Negligible impact** on battery lifetime.

---

## 6. Battery Lifetime Estimate (Ideal)

- **Average current:** 0.0936 mA  
- **Battery capacity:** 3000 mAh  

\[
t_\text{hours} = \frac{3000}{0.0936} \approx 32{,}051\ \text{h} \approx 1{,}335\ \text{days} \approx 3.66\ \text{years}
\]

> Realistic runtime slightly shorter due to self-discharge, temperature effects, and regulator inefficiencies (~2.5–3 years expected).

---

## 7. Timeline Diagram (14-Minutes)


- **Init spike:** short, high-current event  
- **Listen pulse:** low-duty RX  
- **Baseline sleep:** dominates average current

Current (mA) 
^
|
46 ┤ ████ <-- init spike (~50ms)  
|
6.91 ┤ █ <-- listen pulse (~9.91ms, repeats each 1s)  
|
0.025 ┤───────────── <-- baseline sleep (~25µA)  
|
+-------------------------------------------------> Time  
t0 1s 2s 3s ... 840s (~14min)  


---

## 8. Conclusions

1. **Radio.sleep + ESP32-S3 deep sleep** achieves **ultra-low average current (~93.6 µA)**.  
2. **Init spike (46 mA / 50 ms)** is **negligible** compared to battery capacity.  
3. **Battery lifetime for a 3000 mAh LiPo:** >3 years ideal, ~2.5–3 years realistically.  
4. The system is extremely efficient; the listen pulse contributes very little to overall consumption.  

---

*End of Document*


