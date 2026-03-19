import sys

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"

with open(output_panel_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Fix the avg_rate warnings
old_warn1 = """	if (avg_rate > 5)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 빠르게 가고 있습니다.\\n")
			_("  헤어스프링 장력이 강하거나 레귤레이터가\\n")
			_("  \\\"F(Fast)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n"));
	else if (avg_rate < -10)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 느리게 가고 있습니다.\\n")
			_("  오일 경화로 인한 마찰 또는 레귤레이터가\\n")
			_("  \\\"S(Slow)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n"));
	else
		atv_append(buf,&it,"tag_good",
			_("  평균 레이트가 우수한 범위에 있습니다.\\n")
			_("  시계의 전반적인 조율 상태가 양호합니다.\\n"));"""

new_warn1 = """	if (avg_rate > 5)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 빠르게 가고 있습니다.\\n  헤어스프링 장력이 강하거나 레귤레이터가\\n  \\\"F(Fast)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n"));
	else if (avg_rate < -10)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 느리게 가고 있습니다.\\n  오일 경화로 인한 마찰 또는 레귤레이터가\\n  \\\"S(Slow)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n"));
	else
		atv_append(buf,&it,"tag_good",
			_("  평균 레이트가 우수한 범위에 있습니다.\\n  시계의 전반적인 조율 상태가 양호합니다.\\n"));"""

text = text.replace(old_warn1, new_warn1)

# Fix final_advice array
old_advice = """	const char *final_advice[] = {
		_("\\n  이 시계는 거의 완벽에 가까운 상태입니다.\\n")
		_("  현재 상태를 유지하기 위해 3~5년마다\\n")
		_("  예방적 오버홀을 권장합니다. 🏆\\n"),

		_("\\n  이 시계는 매우 우수한 상태입니다.\\n")
		_("  정기적인 오버홀 주기를 지키면 이 상태를\\n")
		_("  오래 유지할 수 있습니다. ✨\\n"),

		_("\\n  전반적으로 양호한 상태입니다.\\n")
		_("  진폭 또는 자세차 중 한 항목이 주의 수준으로,\\n")
		_("  4년 이내 오버홀을 권장합니다.\\n"),

		_("\\n  시계가 주의가 필요한 상태입니다.\\n")
		_("  오버홀을 통해 오일 교체 및 부품 점검을\\n")
		_("  받으시면 상당한 개선이 기대됩니다.\\n"),

		_("\\n  시계의 상태가 좋지 않습니다.\\n")
		_("  신뢰할 수 있는 시계사에서 오버홀을 받으세요.\\n")
		_("  방치 시 부품 마모가 가속화될 수 있습니다. ⚠️\\n"),

		_("\\n  즉각적인 오버홀이 필요합니다.\\n")
		_("  무브먼트 내부에 심각한 문제가 의심됩니다.\\n")
		_("  전문 시계사 방문을 강력히 권고합니다. 🔴\\n"),
	};"""

new_advice = """	const char *final_advice[] = {
		_("\\n  이 시계는 거의 완벽에 가까운 상태입니다.\\n  현재 상태를 유지하기 위해 3~5년마다\\n  예방적 오버홀을 권장합니다. 🏆\\n"),
		_("\\n  이 시계는 매우 우수한 상태입니다.\\n  정기적인 오버홀 주기를 지키면 이 상태를\\n  오래 유지할 수 있습니다. ✨\\n"),
		_("\\n  전반적으로 양호한 상태입니다.\\n  진폭 또는 자세차 중 한 항목이 주의 수준으로,\\n  4년 이내 오버홀을 권장합니다.\\n"),
		_("\\n  시계가 주의가 필요한 상태입니다.\\n  오버홀을 통해 오일 교체 및 부품 점검을\\n  받으시면 상당한 개선이 기대됩니다.\\n"),
		_("\\n  시계의 상태가 좋지 않습니다.\\n  신뢰할 수 있는 시계사에서 오버홀을 받으세요.\\n  방치 시 부품 마모가 가속화될 수 있습니다. ⚠️\\n"),
		_("\\n  즉각적인 오버홀이 필요합니다.\\n  무브먼트 내부에 심각한 문제가 의심됩니다.\\n  전문 시계사 방문을 강력히 권고합니다. 🔴\\n"),
	};"""

text = text.replace(old_advice, new_advice)

with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(text)

print("Fixed C compilation errors.")
