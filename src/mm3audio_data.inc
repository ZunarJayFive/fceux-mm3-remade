// Default Mega Man 3 Remade mapping and loop points (same format as map.csv / loops.csv in the mm3_remade folder).
// Files with these names in that folder override these defaults.
static const char *MM3_DEFAULT_MAP = R"MM3(# sound_id, stage($22 values, hex, separated by ';' or * for any), album track number
# Lines starting with # are ignored. Remove the leading # on a guess line to enable it.
00,*,02
0E,*,03
10,*,04
33,*,05
01,00,06
01,08,17
02,01,09
03,02,10
03,09,19
04,03,11
05,04,07
06,05,13
07,06,12
07,0A,20
08,07,08
08,0B,18
09,0C,23
09,0D,24
0A,0E,25
0D,11,30
0D,*,26
0C,00;01;02;03;04;05;06;07,14
0C,08;09;0A;0B,21
38,*,15
36,*,22
35,*,16
# Confirmed in Wily 1 and Wily 2: the boss doors send 0D (Wily Castle Boss), not 0C
# Wily 4 (stage 0F) plays sound 0A like Wily 3 (confirmed by capture, no other music in the stage); album track 27 chosen by stage order
0A,0F,27
# Wily 5 and Wily 6 both send 0B; track choice by stage order (unconfirmed by ear)
0B,10,27
0B,11,29
# Wily 6 boss fight sends 0D like the other Wily bosses; mapped to Gamma
# End credits (sound 0F, confirmed by user: Proto Man's Theme)
0F,*,31
# Epilogue (sound 12, Wily castle ending up to the whistle scene), confirmed by user
12,*,31
# Wily 4 Boss Rush: first refight boss starts (A0), eighth is defeated (A1, back to the Wily 4 track)
A0,0F,28
A1,0F,27
)MM3";
static const char *MM3_DEFAULT_LOOPS = R"MM3(# track,loop_start_seconds,loop_end_seconds,name
# Tracks not listed play once with no looping. Edit by ear if a loop point sounds wrong.
03,10.6037,23.3831,Password
04,1.6037,10.1232,Stage Select
06,58.7370,109.8532,Needle Man
07,39.7756,82.9051,Top Man
08,4.1966,57.4403,Shadow Man
09,35.3778,65.7280,Magnet Man
10,14.7369,70.1151,Gemini Man
11,13.5573,65.7389,Hard Man
12,17.7818,43.3400,Spark Man
13,4.5759,42.9130,Snake Man
14,46.0124,69.7716,Boss
16,16.8684,38.1668,Weapon Get!
17,25.1796,65.1142,Needle Doc
18,13.3839,64.4963,Shadow Doc
19,14.8979,84.1170,Gemini Doc
20,37.0573,75.3963,Spark Doc
21,22.5233,67.7834,Doc Boss
23,8.3576,45.6324,Wily 1
24,19.4195,57.7531,Wily 2 (Wily's Underwater Base-Gameboy)
25,41.1502,74.4290,Wily 3
26,26.3514,82.2557,Wily Castle Boss
27,0.2000,42.9160,Wily 4
29,4.2044,55.3222,The Penultimate Battle
)MM3";
