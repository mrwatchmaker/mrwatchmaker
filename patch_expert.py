import sys
import re

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"
i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

strings_to_wrap = [
    r'"  💡  전문가 종합 소견\n"',
    r'"  시계가 하루 평균 빠르게 가고 있습니다.\n"',
    r'"  헤어스프링 장력이 강하거나 레귤레이터가\n"',
    r'"  \"F(Fast)\" 방향으로 치우쳐 있을 수 있습니다.\n"',
    r'"  시계가 하루 평균 느리게 가고 있습니다.\n"',
    r'"  오일 경화로 인한 마찰 또는 레귤레이터가\n"',
    r'"  \"S(Slow)\" 방향으로 치우쳐 있을 수 있습니다.\n"',
    r'"  평균 레이트가 우수한 범위에 있습니다.\n"',
    r'"  시계의 전반적인 조율 상태가 양호합니다.\n"',
    r'"\n  이 시계는 거의 완벽에 가까운 상태입니다.\n"',
    r'"  현재 상태를 유지하기 위해 3~5년마다\n"',
    r'"  예방적 오버홀을 권장합니다. 🏆\n"',
    r'"\n  이 시계는 매우 우수한 상태입니다.\n"',
    r'"  정기적인 오버홀 주기를 지키면 이 상태를\n"',
    r'"  오래 유지할 수 있습니다. ✨\n"',
    r'"\n  전반적으로 양호한 상태입니다.\n"',
    r'"  진폭 또는 자세차 중 한 항목이 주의 수준으로,\n"',
    r'"  4년 이내 오버홀을 권장합니다.\n"',
    r'"\n  시계가 주의가 필요한 상태입니다.\n"',
    r'"  오버홀을 통해 오일 교체 및 부품 점검을\n"',
    r'"  받으시면 상당한 개선이 기대됩니다.\n"',
    r'"\n  시계의 상태가 좋지 않습니다.\n"',
    r'"  신뢰할 수 있는 시계사에서 오버홀을 받으세요.\n"',
    r'"  방치 시 부품 마모가 가속화될 수 있습니다. ⚠️\n"',
    r'"\n  즉각적인 오버홀이 필요합니다.\n"',
    r'"  무브먼트 내부에 심각한 문제가 의심됩니다.\n"',
    r'"  전문 시계사 방문을 강력히 권고합니다. 🔴\n"',
    r'"  MrWatchmaker — 당신의 소중한 타임피스를 위해\n\n"'
]

strings_to_add = [
    ("  💡  전문가 종합 소견\\n", "  💡  Expert's Overall Opinion\\n", "  💡  専門家の総合所見\\n", "  💡  专家综合意见\\n", "  💡  Avis Global de l'Expert\\n"),
    ("  시계가 하루 평균 빠르게 가고 있습니다.\\n", "  The watch is running fast on average.\\n", "  時計が1日平均で速く進んでいます。\\n", "  手表平均每天走得较快。\\n", "  La montre avance en moyenne.\\n"),
    ("  헤어스프링 장력이 강하거나 레귤레이터가\\n", "  Hairspring tension may be strong or the\\n", "  ヒゲゼンマイの張力が強いか、緩急針が\\n", "  游丝张力可能较强，或者快慢针\\n", "  La tension du spiral est peut-être forte ou\\n"),
    ("  \\\"F(Fast)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n", "  regulator may be shifted towards \\\"F(Fast)\\\".\\n", "  「F(Fast)」方向に偏っている可能性があります。\\n", "  可能偏向“F(Fast)”方向。\\n", "  la raquette est décalée vers \\\"F(Fast)\\\".\\n"),
    ("  시계가 하루 평균 느리게 가고 있습니다.\\n", "  The watch is running slow on average.\\n", "  時計が1日平均で遅く進んでいます。\\n", "  手表平均每天走得较慢。\\n", "  La montre retarde en moyenne.\\n"),
    ("  오일 경화로 인한 마찰 또는 레귤레이터가\\n", "  Friction from hardened oil or the regulator\\n", "  オイル硬化による摩擦、または緩急針が\\n", "  可能是油干涸导致摩擦增加，或者快慢针\\n", "  Friction due à l'huile durcie ou la raquette\\n"),
    ("  \\\"S(Slow)\\\" 방향으로 치우쳐 있을 수 있습니다.\\n", "  may be shifted towards \\\"S(Slow)\\\".\\n", "  「S(Slow)」方向に偏っている可能性があります。\\n", "  偏向“S(Slow)”方向。\\n", "  est décalée vers \\\"S(Slow)\\\".\\n"),
    ("  평균 레이트가 우수한 범위에 있습니다.\\n", "  The average rate is in an excellent range.\\n", "  平均日差が優れた範囲内にあります。\\n", "  平均日差处于极佳范围内。\\n", "  La marche moyenne est dans une excellente plage.\\n"),
    ("  시계의 전반적인 조율 상태가 양호합니다.\\n", "  The overall tuning of the watch is good.\\n", "  時計の全体的な調整状態は良好です。\\n", "  手表的整体调校状态良好。\\n", "  Le réglage global de la montre est bon.\\n"),
    ("\\n  이 시계는 거의 완벽에 가까운 상태입니다.\\n", "\\n  This watch is in near perfect condition.\\n", "\\n  この時計はほぼ完璧に近い状態です。\\n", "\\n  这块表处于近乎完美的状态。\\n", "\\n  Cette montre est dans un état presque parfait.\\n"),
    ("  현재 상태를 유지하기 위해 3~5년마다\\n", "  To maintain this state, a preventive\\n", "  現在の状態を維持するために、3〜5年ごとの\\n", "  为了保持这种状态，建议每3到5年\\n", "  Pour le maintenir, une révision préventive\\n"),
    ("  예방적 오버홀을 권장합니다. 🏆\\n", "  overhaul every 3-5 years is recommended. 🏆\\n", "  予防的なオーバーホールをお勧めします。 🏆\\n", "  进行一次预防性保养。 🏆\\n", "  tous les 3 à 5 ans est recommandée. 🏆\\n"),
    ("\\n  이 시계는 매우 우수한 상태입니다.\\n", "\\n  This watch is in very good condition.\\n", "\\n  この時計は非常に優秀な状態です。\\n", "\\n  这块表的状态非常优秀。\\n", "\\n  Cette montre est en très bon état.\\n"),
    ("  정기적인 오버홀 주기를 지키면 이 상태를\\n", "  Keeping to a regular overhaul schedule will\\n", "  定期的なオーバーホール周期を守れば、この状態を\\n", "  坚持定期的保养周期，\\n", "  Respecter un calendrier de révision régulier\\n"),
    ("  오래 유지할 수 있습니다. ✨\\n", "  maintain this state for a long time. ✨\\n", "  長く維持することができます。 ✨\\n", "  可以长时间保持这种状态。 ✨\\n", "  maintiendra cet état longtemps. ✨\\n"),
    ("\\n  전반적으로 양호한 상태입니다.\\n", "\\n  Overall in good condition.\\n", "\\n  全体的に良好な状態です。\\n", "\\n  整体状态良好。\\n", "\\n  Bon état général.\\n"),
    ("  진폭 또는 자세차 중 한 항목이 주의 수준으로,\\n", "  Either amplitude or positional error is at a\\n", "  振り角または姿勢差のどちらかが注意レベルで、\\n", "  摆幅或方位差其中一项处于警告水平，\\n", "  L'amplitude ou l'écart positionnel est à un\\n"),
    ("  4년 이내 오버홀을 권장합니다.\\n", "  warning level. Overhaul within 4 years advised.\\n", "  4年以内のオーバーホールをお勧めします。\\n", "  建议在4年内进行保养。\\n", "  niveau d'alerte. Révision d'ici 4 ans conseillée.\\n"),
    ("\\n  시계가 주의가 필요한 상태입니다.\\n", "\\n  The watch requires attention.\\n", "\\n  時計に注意が必要な状態です。\\n", "\\n  这块表处于需要注意的状态。\\n", "\\n  La montre nécessite de l'attention.\\n"),
    ("  오버홀을 통해 오일 교체 및 부품 점검을\\n", "  Significant improvement is expected from an\\n", "  オーバーホールでオイル交換と部品点検を\\n", "  通过保养更换润滑油和检查零件，\\n", "  Une révision avec vidange d'huile et vérification\\n"),
    ("  받으시면 상당한 개선이 기대됩니다.\\n", "  overhaul with oil change and parts check.\\n", "  受ければ大幅な改善が期待できます。\\n", "  有望获得显著改善。\\n", "  des pièces apportera une nette amélioration.\\n"),
    ("\\n  시계의 상태가 좋지 않습니다.\\n", "\\n  The watch is in poor condition.\\n", "\\n  時計の状態が良くありません。\\n", "\\n  手表的状态不佳。\\n", "\\n  La montre est en mauvais état.\\n"),
    ("  신뢰할 수 있는 시계사에서 오버홀을 받으세요.\\n", "  Get an overhaul from a trusted watchmaker.\\n", "  信頼できる時計店でオーバーホールを受けてください。\\n", "  请在可靠的钟表匠处进行保养。\\n", "  Faites une révision chez un horloger de confiance.\\n"),
    ("  방치 시 부품 마모가 가속화될 수 있습니다. ⚠️\\n", "  Ignoring this may accelerate parts wear. ⚠️\\n", "  放置すると部品の摩耗が加速する可能性があります。 ⚠️\\n", "  如果不加理会，零件磨损可能会加速。 ⚠️\\n", "  L'ignorer peut accélérer l'usure des pièces. ⚠️\\n"),
    ("\\n  즉각적인 오버홀이 필요합니다.\\n", "\\n  Immediate overhaul is required.\\n", "\\n  即時のオーバーホールが必要です。\\n", "\\n  需要立即进行全面保养。\\n", "\\n  Une révision immédiate est requise.\\n"),
    ("  무브먼트 내부에 심각한 문제가 의심됩니다.\\n", "  A serious internal movement problem is suspected.\\n", "  ムーブメント内部に深刻な問題が疑われます。\\n", "  怀疑机芯内部存在严重问题。\\n", "  Un problème grave du mouvement est suspecté.\\n"),
    ("  전문 시계사 방문을 강력히 권고합니다. 🔴\\n", "  Visiting a professional watchmaker is strongly advised. 🔴\\n", "  専門の時計店への訪問を強くお勧めします。 🔴\\n", "  强烈建议您拜访专业的钟表匠。 🔴\\n", "  La visite chez un horloger pro est fortement conseillée. 🔴\\n"),
    ("  MrWatchmaker — 당신의 소중한 타임피스를 위해\\n\\n", "  MrWatchmaker — For your precious timepiece\\n\\n", "  MrWatchmaker — あなたの大切なタイムピースのために\\n\\n", "  MrWatchmaker — 为了您珍贵的时计\\n\\n", "  MrWatchmaker — Pour votre précieux garde-temps\\n\\n")
]

# 1. Update output_panel.c
with open(output_panel_path, 'r', encoding='utf-8') as f:
    op_code = f.read()

count = 0
for s in strings_to_wrap:
    if f"_{s}" not in op_code and f"_({s})" not in op_code:
        if s in op_code:
            op_code = op_code.replace(s, f"_({s})")
            count += 1
        else:
            pass # suppress print to avoid UnicodeEncodeError

with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(op_code)

print(f"Replaced {count} strings in output_panel.c")

# 2. Update i18n.c
with open(i18n_path, 'r', encoding='utf-8') as f:
    i18n_code = f.read()

def insert_dict(content, dict_name, idx):
    match = re.search(r"static dict_entry_t " + dict_name + r"\[\] = \{([\s\S]*?)\{NULL, NULL\}", content)
    if not match:
        return content
    
    dict_content = match.group(1)
    for s in strings_to_add:
        if f'"{s[0]}"' in dict_content:
            continue
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        val_str = s[idx].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{val_str}"}},\n'
    
    new_block = f"static dict_entry_t {dict_name}[] = {{{dict_content}{{NULL, NULL}}"
    return content[:match.start()] + new_block + content[match.end():]

i18n_code = insert_dict(i18n_code, "dict_en", 1)
i18n_code = insert_dict(i18n_code, "dict_ja", 2)
i18n_code = insert_dict(i18n_code, "dict_zh", 3)
i18n_code = insert_dict(i18n_code, "dict_fr", 4)

# For ko
match = re.search(r"static dict_entry_t dict_ko\[\] = \{([\s\S]*?)\{NULL, NULL\}", i18n_code)
if match:
    dict_content = match.group(1)
    for s in strings_to_add:
        if f'"{s[0]}"' in dict_content:
            continue
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{key_str}"}},\n'
    new_block = f"static dict_entry_t dict_ko[] = {{{dict_content}{{NULL, NULL}}"
    i18n_code = i18n_code[:match.start()] + new_block + i18n_code[match.end():]

with open(i18n_path, 'w', encoding='utf-8') as f:
    f.write(i18n_code)

print("Updated output_panel.c and i18n.c with expert opinion strings.")
