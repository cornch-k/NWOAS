# S189 native UEFI P12

Hardware run cpufreq-s189-native-20260910-051312.0p23NQ selected P12 during ReadyToBoot. Windows worker read-only snapshot confirmed statuscc; late host P12 writes omitted. Five full per-core passes,40correctchecksums; Pcores~59ms, Ecores~85..87ms. See hardware-result.json.

S140 initial host cluster init is still present in this control. S193 investigates removing it; do not call S189 completely host-independent. P12 selects an existing normal operating point and does not disable APSC or adjust voltage tables. Dynamic frequency/idle/thermal-driver support remains incomplete. A header comment saying “permanently” is too strong: this survives loader initialization, but subsequent software or reboot can change the setting.

S191 Cinebench process ran~660s thenexited, but its CMDwrapper captured neither score nor nativeexitcode. Do not count that as a completed score sample. First S180 score1647.353 is independently evidenced. S196 uses corrected owned-process output collection.
