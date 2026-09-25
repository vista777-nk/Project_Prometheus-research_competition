#!/usr/bin/env python3
"""MAVLink 2 签名自测 —— 验证签名/验签/防重放，无需真实飞控。

用法::

    python3 src/deployment/mavlink/test-mavlink-signing.py

退出码::

    0  全部断言通过
    1  有断言失败
    2  环境里没有 pymavlink —— 跳过（validate.sh 会报 SKIP，不算失败）

与 task-14 §14B.5 原文的两处差异（两处都会让原脚本跑不通，见 ADR-0010）:

1. 原文只设了 `mav.signing.secret_key`，没设 `sign_outgoing`。
   pymavlink 里只有 `sign_outgoing = True` 才真的签名，只给密钥不签。
   实测: 两次 pack 都是 21 字节，于是原文那句
   `assert signed_len == unsigned_len + 13` 恒为假。
2. 原文用 `mavutil.mavlink`，那是 **MAVLink 1** 的 v10 方言。
   拿它解 MAVLink 2 的签名帧会报
   `invalid MAVLink message length. Got 22 expected 9` —— 一条完全指错方向的
   报错。这里显式用 `dialects.v20.common`。
"""

import os
import sys

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_SKIP = 2

# MAVLink 2 签名块: link_id(1) + timestamp(6) + signature(6)
SIGNATURE_OVERHEAD = 13


def make_link(secret_key, sign_outgoing, link_id=0):
    """构造一个 MAVLink 2 收发端。"""
    from pymavlink.dialects.v20 import common as mavlink2

    link = mavlink2.MAVLink(None, srcSystem=1, srcComponent=1)
    link.signing.secret_key = secret_key
    link.signing.sign_outgoing = sign_outgoing
    link.signing.link_id = link_id
    return link


def build_heartbeat(link):
    """编一条心跳消息（不打包）。"""
    from pymavlink.dialects.v20 import common as mavlink2

    return link.heartbeat_encode(
        type=mavlink2.MAV_TYPE_QUADROTOR,
        autopilot=mavlink2.MAV_AUTOPILOT_PX4,
        base_mode=0,
        custom_mode=0,
        system_status=0,
    )


def check(condition, message):
    """断言并把结果打出来。"""
    if not condition:
        raise AssertionError(message)
    print("  ✓ " + message)


def run() -> int:
    """跑完四项断言，返回退出码。"""
    secret_key = os.urandom(32)
    wrong_key = os.urandom(32)

    sender = make_link(secret_key, sign_outgoing=True)
    plain_sender = make_link(None, sign_outgoing=False)
    message = build_heartbeat(sender)

    unsigned = message.pack(plain_sender)
    signed = message.pack(sender)

    print("1. 签名开销")
    print(f"     未签名帧长: {len(unsigned)} bytes")
    print(f"     签名后帧长: {len(signed)} bytes")
    check(
        len(signed) == len(unsigned) + SIGNATURE_OVERHEAD,
        f"签名开销恰好 {SIGNATURE_OVERHEAD} 字节 "
        f"(link_id 1 + timestamp 6 + signature 6)",
    )

    print("2. 同密钥验签")
    receiver = make_link(secret_key, sign_outgoing=False)
    decoded = receiver.decode(bytearray(signed))
    check(decoded is not None, "同密钥可以解出消息")
    check(
        decoded.get_type() == "HEARTBEAT",
        "解出的消息类型正确 (HEARTBEAT)",
    )

    print("3. 错误密钥必须被拒")
    attacker_view = make_link(wrong_key, sign_outgoing=False)
    try:
        attacker_view.decode(bytearray(signed))
        raise AssertionError("换了密钥竟然也验签通过 —— 签名形同虚设")
    except AssertionError:
        raise
    except Exception as error:  # pymavlink 抛的是方言内部的 MAVError
        check(
            "signature" in str(error).lower(),
            f"错误密钥被拒: {error}",
        )

    print("4. 重放必须被拒")
    # 同一帧再解一次: 时间戳没有前进, 按 MAVLink 2 的规则应当被丢弃。
    try:
        receiver.decode(bytearray(signed))
        raise AssertionError("同一帧重放竟然通过了 —— 防重放没生效")
    except AssertionError:
        raise
    except Exception as error:
        check("signature" in str(error).lower(), f"重放帧被拒: {error}")

    print("5. 未签名帧必须被拒")
    # allow_unsigned 默认关闭, 对应 mavros_signing.yaml 里的 allow_unsigned: false
    strict_receiver = make_link(secret_key, sign_outgoing=False)
    try:
        strict_receiver.decode(bytearray(unsigned))
        raise AssertionError("未签名帧竟然被接受 —— 攻击者可以直接绕过签名")
    except AssertionError:
        raise
    except Exception as error:
        check("signature" in str(error).lower(), f"未签名帧被拒: {error}")

    print("\nMAVLink 2 签名自测全部通过")
    return EXIT_OK


def main() -> int:
    """入口。缺 pymavlink 时报 SKIP 而不是失败。"""
    try:
        import pymavlink  # noqa: F401
    except ImportError:
        print("SKIP: 未安装 pymavlink (pip install -r requirements.txt)")
        return EXIT_SKIP
    try:
        return run()
    except AssertionError as error:
        print(f"\n✗ 断言失败: {error}", file=sys.stderr)
        return EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
