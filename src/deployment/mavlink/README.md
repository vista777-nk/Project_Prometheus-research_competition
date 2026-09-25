# MAVLink 2 消息签名 (task-14 Part B)

数传链路是这套系统里唯一**离开机身、走无线电、且能下达控制指令**的通道。
MAVLink 2 的消息签名防的是三件事：

| 威胁 | 签名如何拦住 |
|------|-------------|
| 数传链路劫持（注入伪造 MAVLink 消息） | 没有密钥就算不出正确签名 |
| 重放攻击（录一段"解锁"再放一遍） | 签名里带 6 字节时间戳，不递增就丢弃 |
| 未授权地面站接管 | 未签名消息一律拒收 |

三条都由 `test-mavlink-signing.py` 逐条断言，本地实测通过（见下）。

---

## 文件

| 文件 | 作用 | 状态 |
|------|------|------|
| `generate-mavlink-key.sh` | 生成 32 字节密钥，写到仓库**外面** | ✅ 可用 |
| `test-mavlink-signing.py` | 签名/验签/防重放自测，无需飞控 | ✅ 5/5 通过 |
| `px4-signing.params` | PX4 数传参数模板 | ⚠ 签名段待实机核实 |
| `../../air_ground_drone_bringup/config/mavros_signing.yaml` | MAVROS 侧配置 | ⚠ 参数名待实机核实 |

两个 ⚠ 不是"还没写完"，是**刻意没写**：参数名无法在离线环境核实，
凭印象填一个名字的后果是导入成功、签名没开。理由与处置见
[ADR-0010](../../../docs/decisions/ADR-0010.md)。

---

## 自测

```bash
python3 src/deployment/mavlink/test-mavlink-signing.py
```

2026-07-31 本地实测输出（pymavlink 2.4.49）：

```
1. 签名开销
     未签名帧长: 21 bytes
     签名后帧长: 34 bytes
  ✓ 签名开销恰好 13 字节 (link_id 1 + timestamp 6 + signature 6)
2. 同密钥验签          ✓
3. 错误密钥必须被拒    ✓ Invalid signature
4. 重放必须被拒        ✓ Invalid signature
5. 未签名帧必须被拒    ✓ Invalid signature
```

自测也在 CI 里跑（`validate-deployment` job → `validate.sh` §2）。

---

## 密钥生命周期

```
生成 (开发机)          分发 (SSH)                使用 (实机)
──────────────         ──────────                ──────────
generate-mavlink-key   scp → /tmp                车机 Pi:   /etc/air-ground/mavlink_secret.key
  ↓                    sudo install -m 600       Pixhawk:   QGC 安全通道设置
~/.config/air-ground/  rm /tmp/...               地面站:    QGC 同一把密钥
mavlink_secret.key
```

**四条红线**：

1. 密钥**绝不进 Git**。脚本默认写到 `~/.config/air-ground/`，且拒绝往工作区写；
   `.gitignore` 拦 `*_secret.key` / `*.secret` 作为第二道防线；
   `validate.sh` §5 会扫 `src/deployment/` 下有没有 `*.key` 作为第三道。
2. 密钥**绝不进 `.params`**。那个文件是明文、会被导出、会被贴进工单。
3. 分发只走 SSH（`scp` + `install -m 600`），不走聊天软件、邮件、网盘。
4. 轮换（建议一季度一次）必须**同一时间更新全部节点**：
   密钥不匹配的节点会立刻全部失联，而症状是"链路好好的但什么都收不到"。

---

## 启用时机

Phase 1 只准备脚本与模板，**不在仿真里启用**。理由见
[ADR-0010](../../../docs/decisions/ADR-0010.md) §决策-2：
签名失败的表现是"链路正常但消息全丢"，在仿真阶段引入它，
只会把调试成本花在一个当前没有攻击面的问题上（SITL 全在 localhost）。

启用点定在**实机首次通电联调**（task-15），且必须先在地面完成一次
"拔掉签名 → 通、插上签名 → 通"的对照，再上飞。
