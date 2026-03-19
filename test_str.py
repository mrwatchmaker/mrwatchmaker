import sys

with open(r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c", "rb") as f:
    i18n_data = f.read()

with open(r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c", "rb") as f:
    op_data = f.read()

s1 = '  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n'.encode('utf-8')

print("i18n.c has s1:", s1 in i18n_data)
print("output_panel.c has s1:", s1 in op_data)

# Let's find exactly what's in output_panel.c
idx = op_data.find(b'\xed\x8f\x89\xea\xb7\xa0 \xeb\xa0\x88\xec\x9d\xb4\xed\x8a\xb8\xea\xb0\x80 \xec\x9a\xb0\xec\x88\x98\xed\x95\x9c')
if idx != -1:
    end_idx = op_data.find(b'")', idx)
    actual_str = op_data[idx:end_idx]
    print("Actual string in output_panel.c:")
    print(actual_str)

idx2 = i18n_data.find(b'\xed\x8f\x89\xea\xb7\xa0 \xeb\xa0\x88\xec\x9d\xb4\xed\x8a\xb8\xea\xb0\x80 \xec\x9a\xb0\xec\x88\x98\xed\x95\x9c')
if idx2 != -1:
    end_idx2 = i18n_data.find(b'",', idx2)
    actual_str2 = i18n_data[idx2:end_idx2]
    print("Actual string in i18n.c:")
    print(actual_str2)
    
    print("Exact match?", actual_str == actual_str2)

