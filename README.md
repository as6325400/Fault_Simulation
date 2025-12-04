# Fault Simulation

本專案提供一套流程來解析 `ISCAS-85` 底下的電路、生成 pattern 以及 golden，並驗證外部 fault simulator 的輸出是否正確。系統以 **full fault simulation** 為核心：對每個 pattern、每條 net 進行 stuck-at-0 及 stuck-at-1 模擬，評估輸出是否與無故障結果一致。

## 檔案與資料格式

- `testcases/<ckt>.v`：原始電路（ISCAS-85 格式）。
- `testcases/<ckt>.in`：輸入 pattern 與對應的輸出，格式為  
  ```
  net1=val1, net2=val2, ... | out1=valA, out2=valB, ...
  ```
  左側為所有 primary input 的賦值，右側為已知的 primary output。
- `testcases/<ckt>.ans`：full fault simulation 的結果，每行  
  `pattern_index net stuck_at_0_eq stuck_at_1_eq`。`1` 代表注入該 stuck fault 後輸出與 golden 完全相同、`0` 則代表可觀測差異。
- `testcases/<ckt>.ans.sha`：對 `.ans` 檔的 SHA-256 digest（只含十六進位字串，無檔名）。

## 建置

```bash
make            # 產出 bin/main 與 generator/pattern
```

### 主要可執行檔

| 指令 | 說明 |
|------|------|
| `./bin/main <ckt> <output>` | 讀取 `testcases/<ckt>.in`，依規則跑 full fault simulation，並把 `.ans` 內容輸出到 `<output>`。不會修改原測資。內部 fault 演算法透過共用介面注入，可替換 baseline、bit-parallel 或你自訂的版本。 |
| `./generator/pattern <ckt> [count=100] [seed=42]` | 依據 `testcases/<ckt>.v` 產生 `count` 個 pattern，透過簡單 RNG（可指定 seed）填值，將 `inputs | outputs` 寫入 `testcases/<ckt>.in`，並同步產生 `testcases/<ckt>.ans` 與 `.ans.sha`。預設會使用 baseline 模擬器計算 golden output，再用 bit-parallel fault 模擬器寫 `.ans`。 |

> `ckt` 可輸入 `c17` 或 `c17.v`，程式會自動補上 `.v` 並存取 `testcases/` 目錄。

## 批次工具

### 1. 一次產生所有測資

```
./scripts/generate_all.sh [config]
```

- 預設讀取 `config/pattern_targets.txt`，每行 `circuit pattern_count [seed]`。
- 指令會依序呼叫 `generator/pattern`，更新對應的 `.in` / `.ans` / `.ans.sha`。
- 大型電路（例如 c5315、c6288、c7552）因 fault 數量龐大，執行時間較長。

### 2. 測資驗證（SHA 比對 + 時間量測）

```
./scripts/judge.sh <program> <ckt1> [ckt2 ...]
```

- `<program>` 通常填 `./bin/main` 或你的 fault simulator。
- 指令會對每個 `ckt` 執行 `/usr/bin/time -p`（或 `time -p`）量測 real time，將輸出寫入暫存檔後計算 SHA-256，並與 `testcases/<ckt>.ans.sha` 比較。
- 成功會顯示 `OK (sha match, real X.XXs)`，失敗會列出預期與實際的 digest。

## 工作流程建議

1. `make`：建置所有工具。
2. `./generator/pattern <ckt> ...` 或 `./scripts/generate_all.sh`：更新測資與黃金答案。
3. `./bin/main <ckt> output.ans`：實作完成後可自行產生 `.ans` 比較。
4. `./scripts/judge.sh ./bin/main c17 c432 ...`：確認產出與官方 `ans.sha` 一致。

## 額外注意事項

- `generator/pattern` 會一併檢查輸入 `.in` 中的輸出欄位是否與新的模擬結果一致。
- 產生 `.ans` 後會立即寫入 `.ans.sha`，內容為單行 SHA-256 字串，日後 judge 直接比對即可。

## 演算法擴充指南

- Fault 模擬器介面位於 `src/algorithm/fault_simulator.hpp`，定義 `netNames()` 與 `evaluate(pattern)` 兩個虛擬函式。任何新演算法只要繼承這個 base class 並實作上述方法即可接入整套流程。
- 目前提供：
  - `BaselineSimulator`：序列 stuck-at 模擬，用於生成 `.in` 的 golden output。
  - `BitParallelSimulator`：64-bit 併行 stuck-at 模擬，產生 `.ans`。
- 若要加入新演算法：
  1. 在 `src/algorithm/` 建立新的類別，繼承 `FaultSimulator`。
  2. 在 `main`、`generator` 或其他呼叫處 include 新類別並建立物件。
  3. 將物件（以 `FaultSimulator&`）傳給 `io::writeAnswerFile()`，即可產生新的 `.ans`。

## `.ans` Output 格式

- 第一行為標頭：`# pattern_index net stuck_at_0_eq stuck_at_1_eq`
- 後續每行格式：
  ```
  <pattern_index> <net_name> <stuck_at_0_eq> <stuck_at_1_eq>
  ```
  - `pattern_index`：pattern 在 `.in` 中的索引（0-based）。
  - `net_name`：注入 fault 的 net。
  - `stuck_at_0_eq` / `stuck_at_1_eq`：若注入對應 stuck fault 後輸出與 golden 完全一樣則為 `1`，否則 `0`。
- `.ans.sha` 為 `.ans` 的 SHA-256（十六進位）摘要，judge 會用它來驗證答案。
