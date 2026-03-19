import sys
import re

i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"
output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"

strings = [
    # General
    ("9시 (기본)", "9 o'clock (Base)", "9時 (基本)", "9点 (基础)", "9 heures (Base)"),
    ("12시", "12 o'clock", "12時", "12点", "12 heures"),
    ("3시", "3 o'clock", "3時", "3点", "3 heures"),
    ("6시", "6 o'clock", "6時", "6点", "6 heures"),
    ("커스텀 1", "Custom 1", "カスタム 1", "自定义 1", "Personnalisé 1"),
    ("커스텀 2", "Custom 2", "カスタム 2", "自定义 2", "Personnalisé 2"),
    ("커스텀 3", "Custom 3", "カスタム 3", "自定义 3", "Personnalisé 3"),
    ("커스텀 4", "Custom 4", "カスタム 4", "自定义 4", "Personnalisé 4"),
    ("자세차 진단 리포트", "Positional Diagnostic Report", "姿勢差診断レポート", "方位偏差诊断报告", "Rapport de Diagnostic Positionnel"),
    
    # Headers and labels
    ("║    🕰  MrWatchmaker  자세차 진단 리포트    ║\\n", "║   🕰  MrWatchmaker  Positional Report   ║\\n", "║     🕰  MrWatchmaker  姿勢差診断レポート     ║\\n", "║     🕰  MrWatchmaker  方位偏差诊断报告     ║\\n", "║    🕰  MrWatchmaker  Rapport Positionnel   ║\\n"),
    ("  — 종합 등급: ( 미측정 — 자세별 측정 후 표시됩니다 )\\n\\n", "  — Overall Grade: ( Unmeasured — Complete positional tests )\\n\\n", "  — 総合評価: ( 未測定 — 姿勢別測定後に表示されます )\\n\\n", "  — 综合评级: ( 未测量 — 请完成各方位测量 )\\n\\n", "  — Note Globale: ( Non mesuré — Terminez les tests positionnels )\\n\\n"),
    ("  %s 종합 등급: ", "  %s Overall Grade: ", "  %s 総合評価: ", "  %s 综合评级: ", "  %s Note Globale: "),
    ("[ %s급 ]", "[ Grade %s ]", "[ %s級 ]", "[ %s级 ]", "[ Grade %s ]"),
    
    # Grades
    ("마스터피스 수준 — 탁월합니다", "Masterpiece — Excellent", "マスターピース — 卓越しています", "大师级 — 极佳", "Chef-d'œuvre — Excellent"),
    ("매우 우수 — 컬렉터 등급", "Very Good — Collector's Grade", "非常に優秀 — コレクター等級", "非常优秀 — 收藏级", "Très Bon — Qualité Collection"),
    ("양호 — 일상 사용에 충분", "Good — Sufficient for daily use", "良好 — 日常使用に十分", "良好 — 适合日常使用", "Bon — Suffisant au quotidien"),
    ("보통 — 점검을 고려하세요", "Fair — Consider inspection", "普通 — 点検を考慮してください", "一般 — 建议检查", "Moyen — Envisagez une inspection"),
    ("주의 — 전문 점검 권장", "Warning — Professional check recommended", "注意 — 専門的な点検を推奨", "警告 — 建议专业检查", "Attention — Vérification pro recommandée"),
    ("정비 필요 — 즉시 오버홀 권장", "Service Needed — Overhaul recommended", "整備必要 — 即時のオーバーホールを推奨", "需要维修 — 建议立即保养", "Service Requis — Révision immédiate"),
    
    # Stats
    ("  ⏱  자세차(최대편차)   :  %.1f s/d\\n", "  ⏱  Pos. Error (Max Delta) :  %.1f s/d\\n", "  ⏱  姿勢差 (最大偏差)      :  %.1f s/d\\n", "  ⏱  方位差 (最大偏差)      :  %.1f s/d\\n", "  ⏱  Écart Positionnel      :  %.1f s/d\\n"),
    ("  ⚡  평균 진폭          :  %.0f°\\n", "  ⚡  Avg Amplitude          :  %.0f°\\n", "  ⚡  平均振り角             :  %.0f°\\n", "  ⚡  平均摆幅               :  %.0f°\\n", "  ⚡  Amplitude Moyenne      :  %.0f°\\n"),
    ("  💓  평균 비트에러      :  %.2f ms\\n", "  💓  Avg Beat Error         :  %.2f ms\\n", "  💓  平均ビートエラー       :  %.2f ms\\n", "  💓  平均偏振               :  %.2f ms\\n", "  💓  Repère Moyen           :  %.2f ms\\n"),
    ("  📌  평균 레이트        :  %+.1f s/d\\n", "  📌  Avg Rate               :  %+.1f s/d\\n", "  📌  平均日差               :  %+.1f s/d\\n", "  📌  平均日差               :  %+.1f s/d\\n", "  📌  Marche Moyenne         :  %+.1f s/d\\n"),
    
    # Table
    ("  📋  자세별 측정 결과\\n", "  📋  Positional Results\\n", "  📋  姿勢別測定結果\\n", "  📋  各方位测量结果\\n", "  📋  Résultats Positionnels\\n"),
    ("  자세         레이트      진폭    비트에러\\n", "  Position       Rate        Amp      BE\\n", "  姿勢          日差       振り角   ビートエラー\\n", "  方位          日差       摆幅     偏振\\n", "  Position      Marche     Amp      Repère\\n"),
    
    # Analysis Error
    ("  측정 데이터가 없어 상세 분석을 표시할 수 없습니다.\\n  자세별 측정을 완료하면 등급과 분석이 표시됩니다.\\n", "  No data available for detailed analysis.\\n  Complete positional measurements to view analysis.\\n", "  測定データがないため、詳細な分析を表示できません。\\n  姿勢別測定を完了すると評価と分析が表示されます。\\n", "  缺乏测量数据，无法显示详细分析。\\n  完成方位测量后将显示评级和分析。\\n", "  Aucune donnée pour l'analyse détaillée.\\n  Terminez les mesures positionnelles pour l'analyse.\\n"),
    
    # Positional Error analysis
    ("  🔍  자세차 분석\\n", "  🔍  Positional Error Analysis\\n", "  🔍  姿勢差分析\\n", "  🔍  方位偏差分析\\n", "  🔍  Analyse Écart Positionnel\\n"),
    ("  가장 느린 자세 : %s (%.1f s/d)\\n", "  Slowest Pos : %s (%.1f s/d)\\n", "  最も遅い姿勢 : %s (%.1f s/d)\\n", "  最慢方位 : %s (%.1f s/d)\\n", "  Plus Lente : %s (%.1f s/d)\\n"),
    ("  가장 빠른 자세 : %s (%.1f s/d)\\n\\n", "  Fastest Pos : %s (%.1f s/d)\\n\\n", "  最も速い姿勢 : %s (%.1f s/d)\\n\\n", "  最快方位 : %s (%.1f s/d)\\n\\n", "  Plus Rapide : %s (%.1f s/d)\\n\\n"),
    
    ("  ✅ 자세 간 편차 5 s/d 이하 — 환상적입니다.\\n     밸런스 휠의 동적 균형이 완벽에 가까운\\n     수준입니다. 최상급 무브먼트 조율 상태.\\n", "  ✅ Pos. Delta <= 5 s/d — Fantastic.\\n     Balance wheel dynamic poising is near\\n     perfect. Top-tier movement tuning.\\n", "  ✅ 姿勢差 5 s/d 以下 — 素晴らしいです。\\n     テンプの動的バランスが完璧に近い\\n     状態です。最高レベルの調整状態。\\n", "  ✅ 方位差 <= 5 s/d — 极佳。\\n     摆轮动态平衡近乎完美。\\n     顶级机芯调校水平。\\n", "  ✅ Écart <= 5 s/d — Fantastique.\\n     L'équilibrage dynamique est parfait.\\n     Réglage de très haut niveau.\\n"),
    ("  ✅ 자세 간 편차 10 s/d 이하 — 매우 훌륭합니다.\\n     중력에 의한 오차가 잘 억제되어 있으며,\\n     일상 착용 시 정밀한 시간을 유지합니다.\\n", "  ✅ Pos. Delta <= 10 s/d — Very Good.\\n     Gravity errors are well suppressed,\\n     maintains precise time in daily wear.\\n", "  ✅ 姿勢差 10 s/d 以下 — 非常に優れています。\\n     重力による誤差がよく抑えられており、\\n     日常着用で正確な時間を維持します。\\n", "  ✅ 方位差 <= 10 s/d — 非常优秀。\\n     重力误差得到了很好的抑制，\\n     日常佩戴时能保持精准时间。\\n", "  ✅ Écart <= 10 s/d — Très Bon.\\n     Les erreurs de gravité sont supprimées,\\n     maintient une heure précise au quotidien.\\n"),
    ("  🟡 자세 간 편차 20 s/d 이하 — 양호한 상태.\\n     소폭의 윤활 열화나 피봇 마모가 있을 수\\n     있습니다. 4~5년 주기 점검을 권장합니다.\\n", "  🟡 Pos. Delta <= 20 s/d — Good Condition.\\n     Slight lubrication degradation or pivot\\n     wear possible. 4-5 year service recommended.\\n", "  🟡 姿勢差 20 s/d 以下 — 良好な状態。\\n     わずかな潤滑劣化や摩耗がある可能性\\n     があります。4〜5年周期の点検を推奨。\\n", "  🟡 方位差 <= 20 s/d — 状态良好。\\n     可能存在轻微的润滑退化或轴心磨损。\\n     建议4-5年周期保养。\\n", "  🟡 Écart <= 20 s/d — Bon État.\\n     Légère dégradation de l'huile ou usure.\\n     Révision recommandée tous les 4-5 ans.\\n"),
    ("  🟠 자세 간 편차 30 s/d 이하 — 보통 수준.\\n     무브먼트 내 오일 경화 또는 밸런스 스프링\\n     변형 가능성이 있습니다. 점검을 권장합니다.\\n", "  🟠 Pos. Delta <= 30 s/d — Fair.\\n     Oil hardening or balance spring deformation\\n     possible. Inspection recommended.\\n", "  🟠 姿勢差 30 s/d 以下 — 普通レベル。\\n     オイルの硬化やヒゲゼンマイの変形の\\n     可能性があります。点検を推奨します。\\n", "  🟠 方位差 <= 30 s/d — 一般水平。\\n     机芯内润滑油干涸或游丝可能变形。\\n     建议进行检查。\\n", "  🟠 Écart <= 30 s/d — Moyen.\\n     Durcissement de l'huile ou déformation\\n     du spiral possibles. Inspection conseillée.\\n"),
    ("  ⚠️  자세 간 편차 45 s/d 이하 — 점검이 필요합니다.\\n     피봇 마모 또는 보석(jewel) 손상 가능성이\\n     있습니다. 가까운 시계사에 방문 권장.\\n", "  ⚠️  Pos. Delta <= 45 s/d — Check Needed.\\n     Pivot wear or jewel damage possible.\\n     Visit a watchmaker soon.\\n", "  ⚠️  姿勢差 45 s/d 以下 — 点検が必要です。\\n     摩耗や石(ジュエル)の損傷の可能性\\n     があります。時計店への訪問を推奨。\\n", "  ⚠️  方位差 <= 45 s/d — 需要检查。\\n     可能存在轴心磨损或宝石轴承损坏。\\n     建议尽快联系制表师。\\n", "  ⚠️  Écart <= 45 s/d — Vérification requise.\\n     Usure de pivot ou rubis endommagé.\\n     Consultez un horloger bientôt.\\n"),
    ("  🔴 자세 간 편차 45 s/d 초과 — 즉시 정비가 필요합니다.\\n     무브먼트 내부에 심각한 마모나 손상이\\n     의심됩니다. 신뢰할 수 있는 시계사에서\\n     오버홀을 받으세요.\\n", "  🔴 Pos. Delta > 45 s/d — Immediate Service.\\n     Severe wear or damage inside the movement\\n     suspected. Get a professional overhaul.\\n", "  🔴 姿勢差 45 s/d 超過 — 即時整備が必要です。\\n     ムーブメント内部に深刻な摩耗や損傷が\\n     疑われます。オーバーホールを受けてください。\\n", "  🔴 方位差 > 45 s/d — 需要立即维修。\\n     怀疑机芯内部存在严重磨损或损坏。\\n     请寻找可靠的制表师进行全面保养。\\n", "  🔴 Écart > 45 s/d — Service Immédiat.\\n     Usure sévère ou dommages à l'intérieur.\\n     Révision professionnelle requise.\\n"),
    
    # Amplitude analysis
    ("\\n  ⚡  진폭(에너지) 분석\\n", "\\n  ⚡  Amplitude (Energy) Analysis\\n", "\\n  ⚡  振り角(エネルギー)分析\\n", "\\n  ⚡  摆幅(能量)分析\\n", "\\n  ⚡  Analyse de l'Amplitude\\n"),
    ("  ✅ 270° 이상 — 파워가 넘칩니다.\\n     메인스프링의 탄성이 완벽하게 유지되어\\n     있으며, 오일 상태도 양호합니다.\\n", "  ✅ > 270° — Very Powerful.\\n     Mainspring elasticity is perfectly\\n     maintained, oil condition is good.\\n", "  ✅ 270° 以上 — パワーが溢れています。\\n     メインスプリングの弾性が完全に維持\\n     されており、オイルの状態も良好です。\\n", "  ✅ > 270° — 动力充沛。\\n     发条弹力保持完美，\\n     润滑油状态良好。\\n", "  ✅ > 270° — Très Puissant.\\n     Le ressort moteur est parfait,\\n     l'état de l'huile est bon.\\n"),
    ("  ✅ 240~270° — 이상적인 진폭 범위입니다.\\n     무브먼트에 충분한 에너지가 공급되고 있어\\n     안정적인 조속 기능을 기대할 수 있습니다.\\n", "  ✅ 240~270° — Ideal Amplitude Range.\\n     Sufficient energy is supplied, expect\\n     stable timekeeping performance.\\n", "  ✅ 240~270° — 理想的な振り角です。\\n     十分なエネルギーが供給されており、\\n     安定した調速機能が期待できます。\\n", "  ✅ 240~270° — 理想的摆幅范围。\\n     机芯获得了充足的能量供应，\\n     走时性能稳定。\\n", "  ✅ 240~270° — Amplitude Idéale.\\n     Énergie suffisante, attendez-vous à\\n     une performance très stable.\\n"),
    ("  🟡 210~240° — 조금 낮은 편입니다.\\n     오일이 점차 굳어가거나 메인스프링\\n     파워리저브가 줄었을 수 있습니다.\\n     3~5년 주기 오버홀 시점을 고려하세요.\\n", "  🟡 210~240° — Slightly Low.\\n     Oil may be hardening or mainspring power\\n     reduced. Consider 3-5 year overhaul.\\n", "  🟡 210~240° — 少し低めです。\\n     オイルが硬化し始めているかパワー\\n     リザーブが低下している可能性。\\n", "  🟡 210~240° — 偏低。\\n     润滑油可能正在干涸，或发条动力减弱。\\n     考虑3-5年周期的全面保养。\\n", "  🟡 210~240° — Légèrement Basse.\\n     L'huile peut durcir ou le ressort faiblir.\\n     Envisagez une révision tous les 3-5 ans.\\n"),
    ("  🟠 180~210° — 진폭이 다소 낮습니다.\\n     오일 경화로 인한 마찰 증가가 의심됩니다.\\n     오버홀을 받으면 진폭이 회복될 수 있습니다.\\n", "  🟠 180~210° — Somewhat Low.\\n     Friction increase due to oil hardening\\n     suspected. Overhaul will restore amplitude.\\n", "  🟠 180~210° — やや低いです。\\n     オイル硬化による摩擦増加が疑われます。\\n     オーバーホールで回復するでしょう。\\n", "  🟠 180~210° — 摆幅较低。\\n     怀疑机芯内油干导致摩擦增加。\\n     全面保养可恢复摆幅。\\n", "  🟠 180~210° — Assez Basse.\\n     Augmentation de la friction suspectée.\\n     Une révision restaurera l'amplitude.\\n"),
    ("  ⚠️  150~180° — 진폭이 매우 낮습니다.\\n     무브먼트에 에너지가 충분히 전달되지 않고\\n     있습니다. 조속 기능이 불안정해질 수 있어\\n     빠른 정비를 권장합니다.\\n", "  ⚠️  150~180° — Very Low Amplitude.\\n     Energy is not properly transferring.\\n     Timekeeping may become unstable.\\n", "  ⚠️  150~180° — 非常に低いです。\\n     エネルギーが十分に伝わっていません。\\n     動作が不安定になる可能性があります。\\n", "  ⚠️  150~180° — 摆幅非常低。\\n     能量传递不畅。走时可能变得不稳定，\\n     建议尽快进行保养。\\n", "  ⚠️  150~180° — Très Basse.\\n     L'énergie ne se transmet pas bien.\\n     La marche peut devenir instable.\\n"),
    ("  🔴 150° 미만 — 위험 수준의 낮은 진폭.\\n     피봇 고착 또는 오일 완전 경화 상태가\\n     의심됩니다. 즉각적인 오버홀이 필요합니다.\\n", "  🔴 < 150° — Dangerously Low Amplitude.\\n     Pivot sticking or complete oil hardening\\n     suspected. Immediate overhaul required.\\n", "  🔴 150° 未満 — 危険レベルの低さです。\\n     ピボットの固着やオイルの完全硬化が\\n     疑われます。即時のオーバーホールが必要。\\n", "  🔴 < 150° — 危险的低摆幅。\\n     怀疑轴心干涸卡死或润滑油完全干涸。\\n     需要立即进行全面保养。\\n", "  🔴 < 150° — Danger.\\n     Pivot collé ou huile complètement dure.\\n     Révision immédiate requise.\\n"),
    
    # Beat Error analysis
    ("\\n  💓  비트에러 분석\\n", "\\n  💓  Beat Error Analysis\\n", "\\n  💓  ビートエラー分析\\n", "\\n  💓  偏振分析\\n", "\\n  💓  Analyse du Repère\\n"),
    ("  ✅ 0.3 ms 이하 — 완벽한 균형 상태.\\n     팔레트 포크(Pallet Fork)가 이상적인\\n     위치에 세팅되어 있습니다.\\n", "  ✅ <= 0.3 ms — Perfect Balance.\\n     Pallet Fork is set in the ideal\\n     dead-center position.\\n", "  ✅ 0.3 ms 以下 — 完璧なバランス。\\n     アンクル(Pallet Fork)が理想的な\\n     位置にセッティングされています。\\n", "  ✅ <= 0.3 ms — 完美平衡。\\n     擒纵叉处于理想的居中位置。\\n\\n", "  ✅ <= 0.3 ms — Équilibre Parfait.\\n     L'ancre est réglée dans la\\n     position idéale.\\n"),
    ("  ✅ 0.3~0.5 ms — 매우 우수합니다.\\n     틱(Tick)과 톡(Tock)의 간격이 균등하여\\n     안정적인 조속 기능을 보장합니다.\\n", "  ✅ 0.3~0.5 ms — Very Good.\\n     Tick and Tock intervals are even,\\n     ensuring stable operation.\\n", "  ✅ 0.3~0.5 ms — 非常に優秀です。\\n     チクタクの間隔が均等で、安定した\\n     動作を保証します。\\n", "  ✅ 0.3~0.5 ms — 非常优秀。\\n     滴答间隔均匀，确保稳定运行。\\n\\n", "  ✅ 0.3~0.5 ms — Très Bon.\\n     Les intervalles Tick/Tock sont égaux,\\n     assurant une marche stable.\\n"),
    ("  🟡 0.5~0.7 ms — 허용 범위 내입니다.\\n     팔레트 포크의 미세 조정으로 개선이\\n     가능합니다. 정기 점검 시 조정 권장.\\n", "  🟡 0.5~0.7 ms — Within Limits.\\n     Can be improved with minor pallet fork\\n     adjustment during regular service.\\n", "  🟡 0.5~0.7 ms — 許容範囲内です。\\n     アンクルの微調整で改善可能です。\\n     定期点検時の調整を推奨。\\n", "  🟡 0.5~0.7 ms — 允许范围内。\\n     可通过微调擒纵叉来改善。\\n     建议在定期保养时调整。\\n", "  🟡 0.5~0.7 ms — Dans les limites.\\n     Peut être amélioré par un léger réglage\\n     lors de la prochaine révision.\\n"),
    ("  🟠 0.7~1.0 ms — 조정이 필요합니다.\\n     비대칭적인 진동으로 인해 자세에 따라\\n     레이트 변동이 커질 수 있습니다.\\n", "  🟠 0.7~1.0 ms — Adjustment Needed.\\n     Asymmetric oscillation may cause higher\\n     rate variation across positions.\\n", "  🟠 0.7~1.0 ms — 調整が必要です。\\n     非対称な振動により、姿勢によって\\n     日差の変動が大きくなる可能性があります。\\n", "  🟠 0.7~1.0 ms — 需要调整。\\n     非对称摆动可能导致不同方位下的\\n     日差波动增大。\\n", "  🟠 0.7~1.0 ms — Réglage Requis.\\n     L'oscillation asymétrique peut causer\\n     des variations selon la position.\\n"),
    ("  ⚠️  1.0~1.5 ms — 팔레트 포크 조정이 필요합니다.\\n     충격이나 낙하로 인해 팔레트 포크가\\n     어긋났을 가능성이 있습니다.\\n", "  ⚠️  1.0~1.5 ms — Pallet Fork Adj Needed.\\n     Pallet fork may have shifted due to\\n     shock or dropping the watch.\\n", "  ⚠️  1.0~1.5 ms — アンクル調整が必要です。\\n     衝撃や落下によりアンクルがずれた\\n     可能性があります。\\n", "  ⚠️  1.0~1.5 ms — 擒纵叉需要调整。\\n     受到冲击或跌落可能导致\\n     擒纵叉发生偏移。\\n", "  ⚠️  1.0~1.5 ms — Réglage Ancre Requis.\\n     L'ancre a pu bouger suite à un choc.\\n\\n"),
    ("  🔴 1.5 ms 초과 — 즉각적인 조정이 필요합니다.\\n     비트에러가 이 수준이면 자세에 따라\\n     시간 오차가 크게 달라집니다.\\n", "  🔴 > 1.5 ms — Immediate Adj Needed.\\n     At this level, timekeeping error will\\n     vary significantly by position.\\n", "  🔴 1.5 ms 超過 — 即時調整が必要です。\\n     このレベルでは、姿勢によって\\n     時間誤差が大きく変わります。\\n", "  🔴 > 1.5 ms — 需要立即调整。\\n     在此等偏振下，不同方位的\\n     走时误差将发生巨大变化。\\n", "  🔴 > 1.5 ms — Réglage Immédiat.\\n     À ce niveau, l'erreur de marche\\n     variera fortement selon la position.\\n")
]

with open(i18n_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Insert the keys for dict_en, dict_ja, dict_zh, dict_fr
def insert_dict(content, dict_name, idx):
    match = re.search(r"static dict_entry_t " + dict_name + r"\[\] = \{([\s\S]*?)\{NULL, NULL\}", content)
    if not match:
        print(f"Error: {dict_name} not found")
        return content
    
    dict_content = match.group(1)
    for s in strings:
        # Check if already present
        if f'"{s[0]}"' in dict_content:
            continue
        # s[0] is Ko, s[1] is En, s[2] is Ja, s[3] is Zh, s[4] is Fr
        # Note: the target is s[idx] except for KO which is the key (s[0]) but we don't add to ko because it's not needed if key==val
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        val_str = s[idx].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{val_str}"}},\n'
    
    new_block = f"static dict_entry_t {dict_name}[] = {{{dict_content}{{NULL, NULL}}"
    return content[:match.start()] + new_block + content[match.end():]

content = insert_dict(content, "dict_en", 1)
content = insert_dict(content, "dict_ja", 2)
content = insert_dict(content, "dict_zh", 3)
content = insert_dict(content, "dict_fr", 4)

# For ko, the key is the value. 
match = re.search(r"static dict_entry_t dict_ko\[\] = \{([\s\S]*?)\{NULL, NULL\}", content)
if match:
    dict_content = match.group(1)
    for s in strings:
        if f'"{s[0]}"' in dict_content:
            continue
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{key_str}"}},\n'
    new_block = f"static dict_entry_t dict_ko[] = {{{dict_content}{{NULL, NULL}}"
    content = content[:match.start()] + new_block + content[match.end():]

with open(i18n_path, 'w', encoding='utf-8') as f:
    f.write(content)

# Patch output_panel.c
with open(output_panel_path, 'r', encoding='utf-8') as f:
    op_code = f.read()

def replace_with_macro(text):
    global op_code
    op_code = op_code.replace(f'"{text}"', f'_("{text}")')

for s in strings:
    replace_with_macro(s[0].replace('\\n', '\n'))
    
with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(op_code)

print("Patched successfully")
