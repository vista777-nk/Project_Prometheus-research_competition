#!/usr/bin/env python3
"""健康判定逻辑的单元测试。

    python3 -m unittest discover -s src/deployment/healthcheck -v
    # 或直接
    python3 src/deployment/healthcheck/test_healthcheck.py

只用标准库 unittest：部署脚本要能在一台刚烧完系统的树莓派上验证，
不该先让人 pip install 一个测试框架。这与固件侧自带极简 Unity
（而不是拉 submodule）是同一条取舍。

这组用例覆盖的是 `agcheck.py` 的纯判定逻辑，**不碰网络**。
真正的 XML-RPC 交互在 `check_nodes.py` 里，那部分只能连上真实 Master 才能验，
已记入 README §已知限制。
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from agcheck import (  # noqa: E402
    EXIT_DEGRADED,
    EXIT_MASTER_DOWN,
    EXIT_OK,
    EXIT_TOPICS_MISSING,
    EXIT_USAGE,
    REQUIRED_TOPICS,
    DegradedState,
    evaluate,
    extract_published_topics,
    format_report,
    parse_master_uri,
    parse_state_file,
    required_topics,
)

CAR_TOPICS = REQUIRED_TOPICS["car"]
DRONE_TOPICS = REQUIRED_TOPICS["drone"]


class TestParseMasterUri(unittest.TestCase):
    """ROS_MASTER_URI 解析。

    这里错得最隐蔽的是"少写 http://"——现象和 Master 挂了很像，
    但根因完全不同，所以要给出可区分的错误信息。
    """

    def test_full_uri(self):
        self.assertEqual(parse_master_uri("http://192.168.1.100:11311"),
                         ("192.168.1.100", 11311))

    def test_hostname_instead_of_ip(self):
        self.assertEqual(parse_master_uri("http://lab-server:11311"),
                         ("lab-server", 11311))

    def test_default_port_when_omitted(self):
        self.assertEqual(parse_master_uri("http://192.168.1.100"),
                         ("192.168.1.100", 11311))

    def test_trailing_slash_tolerated(self):
        self.assertEqual(parse_master_uri("http://192.168.1.100:11311/"),
                         ("192.168.1.100", 11311))

    def test_surrounding_whitespace_tolerated(self):
        # .env 里手写时行尾多个空格是很常见的
        self.assertEqual(parse_master_uri("  http://192.168.1.100:11311  "),
                         ("192.168.1.100", 11311))

    def test_missing_scheme_is_rejected_with_actionable_message(self):
        with self.assertRaises(ValueError) as ctx:
            parse_master_uri("192.168.1.100:11311")
        message = str(ctx.exception)
        self.assertIn("缺少协议头", message)
        # 报错必须直接给出改法, 而不是只说"格式不对"
        self.assertIn("http://192.168.1.100:11311", message)

    def test_empty_is_rejected(self):
        for bad in ("", "   ", None):
            with self.subTest(value=bad):
                with self.assertRaises(ValueError):
                    parse_master_uri(bad)  # type: ignore[arg-type]

    def test_non_numeric_port_is_rejected(self):
        with self.assertRaises(ValueError) as ctx:
            parse_master_uri("http://host:notaport")
        self.assertIn("端口不是数字", str(ctx.exception))

    def test_out_of_range_port_is_rejected(self):
        for bad in ("http://host:0", "http://host:70000"):
            with self.subTest(uri=bad):
                with self.assertRaises(ValueError):
                    parse_master_uri(bad)

    def test_wrong_scheme_is_rejected(self):
        with self.assertRaises(ValueError):
            parse_master_uri("ftp://host:11311")


class TestExtractPublishedTopics(unittest.TestCase):
    """解析 getSystemState 的返回结构。"""

    def test_extracts_topics_with_publishers(self):
        state = [
            [["/car/observation", ["/car_preprocessor"]],
             ["/car/state", ["/car_preprocessor"]]],
            [["/car/cmd_vel", ["/some_subscriber"]]],
            [],
        ]
        self.assertEqual(extract_published_topics(state),
                         {"/car/observation", "/car/state"})

    def test_topic_with_no_publisher_is_excluded(self):
        """只有订阅者、没有发布者的话题不算存在。

        这正是要报出来的故障形态: 订阅方还挂在那里等,
        发布方已经死了。若把它算作"存在", 健康检查就永远发现不了。
        """
        state = [[["/car/observation", []]], [], []]
        self.assertEqual(extract_published_topics(state), set())

    def test_subscribers_and_services_are_ignored(self):
        state = [
            [],
            [["/only/subscribed", ["/n"]]],
            [["/only/a/service", ["/n"]]],
        ]
        self.assertEqual(extract_published_topics(state), set())

    def test_malformed_entries_are_skipped_not_crashed(self):
        """Master 返回结构异常时不能崩 —— 崩了就等于永远没有健康数据。"""
        state = [[["/ok", ["/n"]], [], ["/missing_nodes_field"], None], [], []]
        self.assertEqual(extract_published_topics(state), {"/ok"})

    def test_empty_and_none_are_safe(self):
        for bad in ([], None, [[], [], []]):
            with self.subTest(value=bad):
                self.assertEqual(extract_published_topics(bad), set())


class TestRequiredTopics(unittest.TestCase):
    def test_known_roles(self):
        self.assertEqual(required_topics("car"), CAR_TOPICS)
        self.assertEqual(required_topics("drone"), DRONE_TOPICS)

    def test_unknown_role_returns_empty(self):
        self.assertEqual(required_topics("submarine"), ())

    def test_roles_do_not_share_topics(self):
        """两个角色的必需话题不能重叠。

        重叠意味着一台 Pi 能靠另一台发的话题"蒙混过关"——
        车机边缘节点挂了, 但因为无人机在发同名话题而显示健康。
        """
        self.assertEqual(set(CAR_TOPICS) & set(DRONE_TOPICS), set())


class TestEvaluate(unittest.TestCase):
    def test_healthy_when_master_up_and_all_topics_present(self):
        report = evaluate(master_reachable=True, role="car", published=CAR_TOPICS)
        self.assertEqual(report.exit_code, EXIT_OK)
        self.assertTrue(report.healthy)

    def test_extra_topics_do_not_break_health(self):
        """多出来的话题不该判为异常 —— 实机上必然有一堆 /rosout /tf。"""
        published = list(CAR_TOPICS) + ["/rosout", "/tf", "/mavros/state"]
        self.assertEqual(evaluate(master_reachable=True, role="car",
                                  published=published).exit_code, EXIT_OK)

    def test_master_down_takes_priority(self):
        report = evaluate(master_reachable=False, master_error="Connection refused",
                          role="car", published=[])
        self.assertEqual(report.exit_code, EXIT_MASTER_DOWN)
        self.assertIn("Connection refused", " ".join(report.details))

    def test_master_down_does_not_also_report_missing_topics(self):
        """★ Master 挂了就不再报话题缺失。

        Master 不可达时话题列表必然是空的。若一并报出来, 一次网络抖动
        会同时产生 "Master 不可达" + "三个话题全丢" 四条告警,
        真正的根因被淹在噪声里 —— 这是告警系统最常见的失败方式。
        """
        report = evaluate(master_reachable=False, master_error="timeout",
                          role="car", published=[])
        joined = report.summary + " ".join(report.details)
        for topic in CAR_TOPICS:
            self.assertNotIn(topic, joined,
                             "Master 不可达时不应逐个列出缺失话题")

    def test_partial_topic_loss_is_detected(self):
        report = evaluate(master_reachable=True, role="car",
                          published=[CAR_TOPICS[0]])
        self.assertEqual(report.exit_code, EXIT_TOPICS_MISSING)
        self.assertIn(CAR_TOPICS[1], " ".join(report.details))
        self.assertIn(CAR_TOPICS[2], " ".join(report.details))
        # 已经在的那个不该出现在"缺失"列表里
        self.assertNotIn("缺失: " + CAR_TOPICS[0], " ".join(report.details))

    def test_all_topics_missing(self):
        report = evaluate(master_reachable=True, role="car", published=[])
        self.assertEqual(report.exit_code, EXIT_TOPICS_MISSING)
        self.assertIn("3/3", report.summary)

    def test_wrong_role_topics_do_not_satisfy_car(self):
        """★ 拿无人机的话题冒充车机必须判失败。

        这条防的是 .env 里 ROLE 写错、两台 Pi 连同一个 Master 的情况:
        车机的边缘节点根本没起, 但 Master 上有无人机的话题 ——
        如果判定逻辑只看"话题数量够不够", 就会显示健康。
        """
        report = evaluate(master_reachable=True, role="car", published=DRONE_TOPICS)
        self.assertEqual(report.exit_code, EXIT_TOPICS_MISSING)

    def test_unknown_role_is_usage_error_not_topic_error(self):
        report = evaluate(master_reachable=True, role="submarine", published=[])
        self.assertEqual(report.exit_code, EXIT_USAGE)
        self.assertIn("car", " ".join(report.details))
        self.assertIn("drone", " ".join(report.details))

    def test_failure_reports_carry_next_steps(self):
        """失败报告必须带可执行的下一步。

        边缘节点跑在没有显示器的树莓派上, 一条 "check failed" 对现场
        毫无帮助。所有失败分支都要给出能照着敲的命令。
        """
        failures = [
            evaluate(master_reachable=False, master_error="refused", role="car"),
            evaluate(master_reachable=True, role="car", published=[]),
        ]
        for report in failures:
            with self.subTest(code=report.exit_code):
                joined = " ".join(report.details)
                self.assertTrue(
                    any(hint in joined for hint in ("ping", "journalctl", "docker", "cat ")),
                    f"失败报告里没有可执行的排查步骤: {report.details}",
                )


class TestFormatReport(unittest.TestCase):
    def test_healthy_marked_ok(self):
        report = evaluate(master_reachable=True, role="car", published=CAR_TOPICS)
        text = format_report(report, "2026-07-30T12:00:00")
        self.assertIn("OK:", text)
        self.assertIn("2026-07-30T12:00:00", text)

    def test_failure_marked_fail(self):
        report = evaluate(master_reachable=False, role="car")
        self.assertIn("FAIL:", format_report(report, "2026-07-30T12:00:00"))

    def test_details_are_indented_under_summary(self):
        report = evaluate(master_reachable=True, role="car", published=CAR_TOPICS)
        lines = format_report(report, "t").splitlines()
        self.assertGreater(len(lines), 1)
        for line in lines[1:]:
            self.assertTrue(line.startswith("  "), f"详情行未缩进: {line!r}")


class TestExitCodeContract(unittest.TestCase):
    """退出码是 systemd unit 与 alert.sh 共同依赖的契约。

    改动这些值必须同步改 air-ground-healthcheck.service 与 alert.sh 的
    case 分支, 因此在这里钉死。
    """

    def test_exit_codes_are_distinct_and_stable(self):
        codes = {
            "ok": EXIT_OK,
            "master_down": EXIT_MASTER_DOWN,
            "topics_missing": EXIT_TOPICS_MISSING,
            "usage": EXIT_USAGE,
        }
        codes["degraded"] = EXIT_DEGRADED
        self.assertEqual(len(set(codes.values())), len(codes), "退出码不能重复")
        # 这组值是**跨端契约**: air-ground-healthcheck.service 的 SuccessExitStatus、
        # alert.sh 的 case 分支都硬编码了它们。改这里必须同步改那两处。
        self.assertEqual(codes, {"ok": 0, "master_down": 1, "topics_missing": 2,
                                 "usage": 3, "degraded": 4})


class TestParseStateFile(unittest.TestCase):
    """entrypoint 写下的状态文件解析。"""

    def test_none_is_not_degraded(self):
        # 文件不存在 = 老镜像或还没写。判成降级会让所有存量部署一起变黄。
        self.assertEqual(parse_state_file(None), DegradedState(False, ""))

    def test_empty_is_not_degraded(self):
        self.assertFalse(parse_state_file("").degraded)
        self.assertFalse(parse_state_file("\n\n  \n").degraded)

    def test_flag_zero_is_not_degraded(self):
        self.assertFalse(parse_state_file("AIR_GROUND_DEGRADED=0").degraded)

    def test_flag_one_is_degraded(self):
        state = parse_state_file(
            """# 注释行
AIR_GROUND_ROLE=car
AIR_GROUND_DEGRADED=1
AIR_GROUND_DEGRADED_REASON=car_edge_real.launch 未提供
"""
        )
        self.assertTrue(state.degraded)
        self.assertEqual(state.reason, "car_edge_real.launch 未提供")

    def test_reason_may_contain_equals_and_spaces(self):
        # 原因里有 "=" 很正常 (贴了个命令行), 不该被截断
        state = parse_state_file(
            """AIR_GROUND_DEGRADED=1
AIR_GROUND_DEGRADED_REASON=跑的是 chassis=diff, 缺 real launch"""
        )
        self.assertEqual(state.reason, "跑的是 chassis=diff, 缺 real launch")

    def test_quotes_are_stripped(self):
        state = parse_state_file(
            """AIR_GROUND_DEGRADED_REASON="缺驱动"
AIR_GROUND_DEGRADED="1"
"""
        )
        self.assertTrue(state.degraded)
        self.assertEqual(state.reason, "缺驱动")

    def test_garbage_lines_are_ignored(self):
        # 半截写入的文件 (容器被 kill 在 write 中间) 不该让健康检查崩掉
        self.assertFalse(parse_state_file("""这不是键值对
=
AIR_GROUND_DEG""").degraded)

    def test_false_words_are_not_degraded(self):
        for word in ("no", "false", "False", ""):
            with self.subTest(word=word):
                self.assertFalse(parse_state_file(f"AIR_GROUND_DEGRADED={word}").degraded)


class TestDegradedPrecedence(unittest.TestCase):
    """降级与真故障的优先级 —— 这是本次改动最容易搞反的地方。"""

    DEGRADED = DegradedState(True, "car_edge_real.launch 未提供")

    def test_degraded_reported_when_everything_else_ok(self):
        report = evaluate(master_reachable=True, role="car",
                          published=CAR_TOPICS, degraded=self.DEGRADED)
        self.assertEqual(report.exit_code, EXIT_DEGRADED)
        self.assertFalse(report.healthy)
        self.assertIn("car_edge_real.launch 未提供", " ".join(report.details))

    def test_master_down_outranks_degraded(self):
        # Master 掉了还报"降级"是把根因藏起来
        report = evaluate(master_reachable=False, master_error="timeout",
                          role="car", published=(), degraded=self.DEGRADED)
        self.assertEqual(report.exit_code, EXIT_MASTER_DOWN)

    def test_topics_missing_outranks_degraded(self):
        report = evaluate(master_reachable=True, role="car",
                          published=("/car/state",), degraded=self.DEGRADED)
        self.assertEqual(report.exit_code, EXIT_TOPICS_MISSING)

    def test_unknown_role_outranks_degraded(self):
        report = evaluate(master_reachable=True, role="submarine",
                          published=CAR_TOPICS, degraded=self.DEGRADED)
        self.assertEqual(report.exit_code, EXIT_USAGE)

    def test_default_argument_keeps_old_behaviour(self):
        # 不传 degraded 时行为必须与改动前一致 —— check_topics.py 等调用方没改
        report = evaluate(master_reachable=True, role="car", published=CAR_TOPICS)
        self.assertEqual(report.exit_code, EXIT_OK)

    def test_report_marker_is_degraded_not_fail(self):
        report = evaluate(master_reachable=True, role="car",
                          published=CAR_TOPICS, degraded=self.DEGRADED)
        text = format_report(report, "2026-07-31T00:00:00")
        self.assertIn("DEGRADED:", text)
        self.assertNotIn("FAIL:", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
