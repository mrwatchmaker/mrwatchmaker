import sys

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"

with open(output_panel_path, 'r', encoding='utf-8') as f:
    text = f.read()

replacements = [
    (
        'snprintf(tmp,sizeof(tmp),_("  %s 종합 등급: "), gstar[go]);\n		atv_append(buf,&it,"tag_silver", tmp);',
        '{ char *ts = g_strdup_printf(_("  %s 종합 등급: "), gstar[go]); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("[ %s급 ]"), gname[go]);\n		atv_append(buf,&it, gtag[go], tmp);',
        '{ char *ts = g_strdup_printf(_("[ %s급 ]"), gname[go]); atv_append(buf,&it, gtag[go], ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  ⏱  자세차(최대편차)   :  %.1f s/d\\n"), pos_error);\n	atv_append(buf,&it, gtag[gp], tmp);',
        '{ char *ts = g_strdup_printf(_("  ⏱  자세차(최대편차)   :  %.1f s/d\\n"), pos_error); atv_append(buf,&it, gtag[gp], ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  ⚡  평균 진폭          :  %.0f°\\n"), avg_amp);\n	atv_append(buf,&it, gtag[ga], tmp);',
        '{ char *ts = g_strdup_printf(_("  ⚡  평균 진폭          :  %.0f°\\n"), avg_amp); atv_append(buf,&it, gtag[ga], ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  💓  평균 비트에러      :  %.2f ms\\n"), avg_be);\n	atv_append(buf,&it, gtag[gb], tmp);',
        '{ char *ts = g_strdup_printf(_("  💓  평균 비트에러      :  %.2f ms\\n"), avg_be); atv_append(buf,&it, gtag[gb], ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  📌  평균 레이트        :  %+.1f s/d\\n"), avg_rate);\n	atv_append(buf,&it,"tag_bright", tmp);',
        '{ char *ts = g_strdup_printf(_("  📌  평균 레이트        :  %+.1f s/d\\n"), avg_rate); atv_append(buf,&it,"tag_bright", ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),\n			"  %-11s  %+6.1f s/d  %4.0f°   %.2f ms\\n",\n			pos_names[i], op->pos_rate[i], op->pos_amp[i], op->pos_be[i]);\n		atv_append(buf,&it,"tag_pos", tmp);',
        '{ char *ts = g_strdup_printf("  %-11s  %+6.1f s/d  %4.0f°   %.2f ms\\n", pos_names[i], op->pos_rate[i], op->pos_amp[i], op->pos_be[i]); atv_append(buf,&it,"tag_pos", ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  가장 느린 자세 : %s (%.1f s/d)\\n"),\n		pos_names[min_idx], min_rate);\n	atv_append(buf,&it,"tag_silver", tmp);',
        '{ char *ts = g_strdup_printf(_("  가장 느린 자세 : %s (%.1f s/d)\\n"), pos_names[min_idx], min_rate); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }'
    ),
    (
        'snprintf(tmp,sizeof(tmp),_("  가장 빠른 자세 : %s (%.1f s/d)\\n\\n"),\n		pos_names[max_idx], max_rate);\n	atv_append(buf,&it,"tag_silver", tmp);',
        '{ char *ts = g_strdup_printf(_("  가장 빠른 자세 : %s (%.1f s/d)\\n\\n"), pos_names[max_idx], max_rate); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }'
    )
]

count = 0
for old_s, new_s in replacements:
    if old_s in text:
        text = text.replace(old_s, new_s)
        count += 1
    else:
        print(f"Warning: could not find snippet:\n{old_s}")

with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(text)

print(f"Replaced {count} snprintf with g_strdup_printf")
