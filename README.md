---

# 🌀 LLVM Pass - Fishe Control Flow Obfuscation

本项目实现了一个基于 LLVM Pass 的 **控制流混淆 (Control Flow Flattening)** 插件，
代号 **Fishe**。它通过在函数间插入调度块（dispatcher block）以及随机的不可达假块，
扰乱控制流结构，从而增加反编译难度。

---

## 🧩 功能特点

- ✅ **控制流扁平化**：使用 switch 控制调度执行顺序；
- ✅ **随机伪造 case**：为每个调度器注入若干不可达块 (`unreachable`)；
- ✅ **命令行可配置强度**：可通过选项 `-fishe-fake=N` 控制假块数量；
- ✅ **LLVM 18 可编译通过**，`opt -verify` 不会报错；
- 🧠 适用于 IR 层面的混淆实验或教学研究。

---

## ⚙️ 构建

> 需要 LLVM 18 的开发头文件与 `llvm-config`。

```bash
clang++ -fPIC -shared Pass.cpp -o libPassFishe.so \
  `llvm-config --cxxflags --ldflags --system-libs --libs core passes`
```

---

## 🚀 使用

### 1. 运行 Pass
```bash
opt -load-pass-plugin=./libPassFishe.so -passes="Pass-Fishe" input.ll -o output.ll
```

### 2. 可选参数
| 参数 | 默认值 | 作用 |
|------|---------|------|
| `-fishe-fake=N` | `3` | 为每个函数 dispatcher 生成 N 个假 (unreachable) 块，用于增强混淆强度 |

示例：

```bash
opt -load-pass-plugin=./libPassFishe.so -passes="Pass-Fishe" -fishe-fake=5 input.ll -o output.ll
```

---

## 📁 混淆效果

经过处理后的 IR 将包含：
- 一个或多个 **调度块 (dispatcher block)**；
- 真正的逻辑块（对应原函数体的语句）；
- 多个 **假块 (`fake_x`)**：

```llvm
switch i32 %var, label %fake_0 [
  i32 100 -> label %BB.1
  i32 200 -> label %BB.2
  i32 9999 -> label %fake_1
  i32 8888 -> label %fake_2
]

fake_0:
  %noise = add i32 13, 13
  unreachable

fake_1:
  unreachable
fake_2:
  %trash = add i32 5, 5
  unreachable
```

这些 `fake_*` 块不会被实际执行，
但会让静态分析与反编译看到更复杂的控制流。

---

## 🧰 开发说明

- Pass 名称：`Pass-Fishe`
- 命名空间：匿名（位于 `registerStandardPasses`）。
- 命令行选项通过 `llvm::cl::opt` 定义（`-fishe-fake`）。

### 修改入口
所有核心逻辑位于：
```cpp
CreateNewSwitch(...)
```
其中根据 `FisheFakeCount` 参数生成对应数量的随机假块。

---

## ⚖️ 注意事项

> 本项目仅用于学习 LLVM Pass 开发与混淆技术研究，
> **请勿将其用于任何违反法律或软件许可协议的用途。**

---
