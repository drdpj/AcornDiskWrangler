#!/bin/bash
# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# run_all.sh <build dir> <real images dir: scsi0.dat, M512_1.ADL, M512_3.IMG> [<real tapes dir: *.uef>]
# Builds every test (with the qmake on PATH, as CP/M Disk Wrangler's run_all.sh: for Qt 6 put qmake6 first on PATH as
# qmake) and runs them all with the right data: the self-tests, the independent checkers (acorncheck.py, sidecheck.py,
# retrofilescheck.py), screenshot and xdragtest (under xvfb-run), the core's tests, clitest, convtest, the acorndw
# golden cases (tests/cli/run_cli.sh) and the check that acorndw links no QtGui or QtWidgets. Prints a line per test
# and ends "ALL PASSED" (exit 0) or "FAILED: <list>" (exit 1). Logs, screenshots (<build dir>/shots) and the made test
# data (<build dir>/td) stay in the build dir. The real images are the owner's (not in the repo): see docs/testing.md.
# The real tapes (UEF files, from The Stairway to Hell) are read from the third argument, by default the folder UEF
# beside the real images (testdata/UEF); there must be some. The real Acorn Atom disc and AtomInPC files (series atom)
# are read from the folder ATOM beside the real images (testdata/ATOM).
# It also makes /tmp/drop/proj (the host files guitest2 and screenshot drop): don't run two at once.
S=$(cd "$(dirname "$0")/.." && pwd)
W=${1:?usage: run_all.sh <build dir> <real images dir>}; R=${2:?usage: run_all.sh <build dir> <real images dir>}
for f in scsi0.dat M512_1.ADL M512_3.IMG; do [ -f "$R/$f" ] || { echo "missing $R/$f (the real images: see docs/testing.md)"; exit 2; }; done
R=$(cd "$R" && pwd); Q=qmake
U=${3:-$R/UEF}; ls "$U"/*.uef >/dev/null 2>&1 || { echo "no real tapes in $U (*.uef: see docs/testing.md)"; exit 2; }; U=$(cd "$U" && pwd)
for f in "System BASIC.ssd" FORM40 FORMAT INFALL; do [ -f "$R/ATOM/$f" ] || { echo "missing $R/ATOM/$f (the real Atom data: see docs/testing.md)"; exit 2; }; done
mkdir -p "$W"; W=$(cd "$W" && pwd); B=$W/b; T=$W/td; fail=()
for d in td t shots wat side ct vt cv cli sa drag boot xd g2 tp tapes atom; do rm -rf "${W:?}/$d"; done; rm -f "$W"/*.log
mkdir -p "$B" "$T" "$W/t"
echo "$($Q --version | tail -1)"
for p in "$S"/tests/*.pro "$S"/core/tests/settingstest.pro "$S"/core/tests/xfertest.pro "$S"/core/tests/uitest.pro "$S"/core/tests/devicetest.pro \
         "$S"/core/tests/volumetest.pro "$S"/core/tests/moduletest.pro "$S"/core/tests/importtest.pro "$S"/core/tests/clitest.pro "$S"/acorndw.pro; do
  [ -f "$p" ] || continue                            # volumetest, moduletest: DiskWranglerCore v0.4.0 on; clitest, acorndw: v0.5.0 on; importtest: v0.6.0 on
  t=$(basename "$p" .pro); mkdir -p "$B/$t"
  (cd "$B/$t" && "$Q" "$p" >/dev/null 2>&1 && make -j"$(nproc)" >"$B/$t.log" 2>&1) || fail+=("build:$t")
done
python3 -I "$S/tests/make_testdata.py" all "$T" >/dev/null       # also makes /tmp/drop/proj: guitest2 and screenshot need it
python3 -I "$S/tests/make_testdata.py" tape "$W/tp" >/dev/null   # a tape, apart: detecttest and agreetest take every file of $T
"$B/mkimg/mkimg" "$T" >/dev/null; "$B/floptest/floptest" acorn800 "$T/g.a800" >/dev/null; "$B/floptest/floptest" hybrid640 "$T/f.hybrid" >/dev/null
export QT_QPA_PLATFORM=offscreen
run() { n=$1; shift; timeout 900 "$@" >"$W/$n.log" 2>&1; e=$?
        if [ $e -eq 0 ] && grep -qE "ALL OK|^all ok" "$W/$n.log"; then echo "pass $n"; else echo "FAIL $n (exit $e, see $W/$n.log)"; fail+=("$n"); fi; }
cpy() { cp "$1" "$W/t/$2"; echo "$W/t/$2"; }                 # tests modify their images: always copies
run viewtest "$B/viewtest/viewtest"
run settingstest "$B/settingstest/settingstest"
run xfertest "$B/xfertest/xfertest"                 # DiskWranglerCore: the transfer layer
run uitest "$B/uitest/uitest"                       # DiskWranglerCore: UI helpers and viewers
run devicetest "$B/devicetest/devicetest"           # DiskWranglerCore: containers, Linear, checked saving
[ -f "$S/core/tests/volumetest.pro" ] && run volumetest "$B/volumetest/volumetest"   # DiskWranglerCore v0.4.0 on: Volume, Disc, detection by tiers
[ -f "$S/core/tests/moduletest.pro" ] && run moduletest "$B/moduletest/moduletest"   # DiskWranglerCore v0.4.0 on: the DFS and FAT modules
[ -f "$S/core/tests/importtest.pro" ] && run importtest "$B/importtest/importtest"   # DiskWranglerCore v0.6.0 on: the Importer (dw::xfer)
"$B/detecttest/detecttest" "$T"/* >"$W/detect.log" 2>&1 || fail+=(detecttest)
run plainl "$B/floptest/floptest" plainl "$(cpy "$T/adfs_l.adl" plainl.adl)"
i=0; for s in 360 720 1200 1440; do
  run bpb$s "$B/floptest/floptest" bpb "$(cpy "$T/pc$s.img" bpb$s.img)"
  mkfs.fat -C "$W/t/tab$i.img" $s >/dev/null; run table$i "$B/floptest/floptest" table "$W/t/tab$i.img" $i; i=$((i+1))
done
run real800 "$B/floptest/floptest" real800 "$(cpy "$R/M512_3.IMG" r800.img)"
run real640 "$B/floptest/floptest" real640 "$(cpy "$R/M512_1.ADL" r640.adl)"
for f in blank40.ssd:ssd:400 blank80.ssd:ssd:800 blank40.dsd:dsd:400 blank80.dsd:dsd:800; do
  IFS=: read -r img k n <<<"$f"; run dfs_$img "$B/dfstest/dfstest" "$(cpy "$T/$img" "dfs_$img")" $k $n; done
mkdir -p "$W/wat"; run watfordtest "$B/watfordtest/watfordtest" "$W/wat"
python3 -I "$S/tests/acorncheck.py" "$W"/wat/* >"$W/watcheck.log" 2>&1 && echo "pass acorncheck(watford)" || fail+=("acorncheck(watford)")
hd=$(cpy "$R/scsi0.dat" hd.dat); hm=$(cpy "$R/scsi0.dat" hdmap.dat); python3 -I "$S/tests/make_testdata.py" mapname "$hm" >/dev/null
run selftest "$B/selftest/selftest" "$hd" "$hm"; rm -f "$hd" "$hm"
run dostest "$B/dostest/dostest" "$(cpy "$R/scsi0.dat" hd.dat)"; rm -f "$W/t/hd.dat"
run guitest "$B/guitest/guitest" "$(cpy "$R/scsi0.dat" hd.dat)"; rm -f "$W/t/hd.dat"
G=$W/g2; mkdir -p "$G"; cp "$T/blank40.dsd" "$G/a.dsd"; cp "$T/adfs_l.adl" "$G/b.adl"; mkfs.fat -C "$G/c.img" 720 >/dev/null
cp "$R/scsi0.dat" "$G/d.dat"; cp "$T/trunc40.ssd" "$G/e.ssd"; cp "$T/f.hybrid" "$G/f.hybrid"; cp "$T/g.a800" "$G/g.a800"; cp "$R/M512_1.ADL" "$G/real640.adl"
cp "$W/tp/t.uef" "$G/h.uef"
run guitest2 "$B/guitest2/guitest2" "$G" "$G/real640.adl"; rm -rf "$G"
mkdir -p "$W/side"; run sidetest "$B/sidetest/sidetest" "$W/side" "$(cpy "$R/M512_1.ADL" s640.adl)" "$(cpy "$R/M512_3.IMG" s800.img)"
python3 -I "$S/tests/sidecheck.py" "$W/side" >"$W/sidecheck.log" 2>&1 && echo "pass sidecheck" || fail+=(sidecheck)
mkdir -p "$W/ct"; run containertest "$B/containertest/containertest" "$W/ct" "$T/adfs_l.adl" adfs "$T/adfs_m.adm" adfs "$T/blank80.dsd" dsd \
  "$T/blank40.ssd" ssd "$T/pc720.img" pc "$T/pc1440.img" pc "$T/f.hybrid" hybrid "$T/g.a800" dos800 "$R/M512_1.ADL" hybrid "$R/M512_3.IMG" dos800
if [ -f "$S/tests/agreetest.pro" ]; then                # step 4: detection by tiers gives what the old detection gave, on every image
  run agreetest "$B/agreetest/agreetest" "$T"/* "$R/M512_1.ADL" "$R/M512_3.IMG" "$R/scsi0.dat" "$W"/wat/* "$W"/side/* "$W"/ct/*
  mkdir -p "$W/vt"; run voltest "$B/voltest/voltest" "$W/vt" "$T/adfs_l.adl" "$R/M512_1.ADL" "$T/blank80.dsd" "$T/pc720.img" "$T/g.a800" "$R/scsi0.dat"
  rm -rf "$W/vt"                                     # Acorn's discs through the core's Volume and Disc
fi
# the console program (DiskWranglerCore v0.5.0 on): the core's own CLI test, conversions between every container, the golden-image cases
[ -f "$S/core/tests/clitest.pro" ] && run clitest "$B/clitest/clitest"
mkdir -p "$W/cv"; run convtest "$B/convtest/convtest" "$W/cv" "$R/M512_1.ADL" "$R/M512_3.IMG"
if [ -x "$S/tests/cli/run_cli.sh" ]; then
  TD0_DIR=$W/ct "$S/tests/cli/run_cli.sh" "$B/acorndw/acorndw" "$W/cli" "$R" >"$W/cli.log" 2>&1; tail -1 "$W/cli.log" | grep -q "^ALL PASSED" && echo "pass acorndw golden ($(grep -c '^pass' "$W/cli.log") cases)" || { echo "FAIL acorndw golden (see $W/cli.log)"; fail+=(acorndw-golden); }
  ldd "$B/acorndw/acorndw" | grep -qiE "QtGui|QtWidgets" && { echo "FAIL acorndw links QtGui/QtWidgets"; fail+=(acorndw-ldd); } || echo "pass acorndw links neither QtGui nor QtWidgets"
  # its manual page (the .deb's /usr/share/man/man1/acorndw.1.gz) names every command and option it has, and only those
  bash "$S/core/ci/check-manpage.sh" "$B/acorndw/acorndw" "$S/man/acorndw.1" >"$W/man.log" 2>&1 && echo "pass $(cat "$W/man.log")" || { cat "$W/man.log"; fail+=(acorndw-manpage); }
fi
# Acorn Atom DOS (series atom): the real disc and AtomInPC files, checked by an independent reader
timeout 300 bash "$S/tests/atom_real.sh" "$B/acorndw/acorndw" "$R/ATOM" "$W/atom" >"$W/atom_real.log" 2>&1 && tail -1 "$W/atom_real.log" | grep -q "^ALL OK" \
  && echo "pass atom_real" || { echo "FAIL atom_real (see $W/atom_real.log)"; fail+=(atom_real); }
# step 5j: File > Save As writes ImageDisk, DSK, extended DSK and raw, the same bytes as acorndw convert (needs acorndw and $W/ct's TeleDisk files)
[ -f "$S/tests/saveastest.pro" ] && { mkdir -p "$W/sa"; run saveastest "$B/saveastest/saveastest" "$W/sa" "$B/acorndw/acorndw" "$R/M512_1.ADL" "$W/ct"; rm -rf "$W/sa"; }
rm -rf "$W/ct"                                       # ImageDisk, DSK and TeleDisk copies of Acorn discs (DiskWranglerCore v0.3.0 on)
# tapes (series cfs-1): the cassette filing system on hand-made and real tapes, and the independent reader (cfs_check.py)
run cfstest "$B/cfstest/cfstest" "$U"
mkdir -p "$W/tapes"; "$B/cfstest/cfstest" --write-samples "$W/tapes"
timeout 600 python3 -I "$S/tests/cfs_check.py" "$B/cfstest/cfstest" "$W/tapes" "$U" >"$W/cfscheck.log" 2>&1 \
  && tail -1 "$W/cfscheck.log" | grep -qE "^([0-9]+) of \1 tapes" && echo "pass cfs_check ($(tail -1 "$W/cfscheck.log"))" || { echo "FAIL cfs_check (see $W/cfscheck.log)"; fail+=(cfs_check); }
mkdir -p "$W/drag"; run dragtest "$B/dragtest/dragtest" "$W/drag"
python3 -I "$S/tests/retrofilescheck.py" "$W"/drag/drag-* >"$W/rfc.log" 2>&1 && echo "pass retrofilescheck" || fail+=(retrofilescheck)
mkdir -p "$W/boot"; run boottest "$B/boottest/boottest" "$W/boot" "$(cpy "$R/scsi0.dat" bhd.dat)"; rm -f "$W/t/bhd.dat"
python3 -I "$S/tests/acorncheck.py" "$W"/boot/* >"$W/bootcheck.log" 2>&1        # boot_hyb_old.adl must be the only FAIL
[ "$(grep -c 'FAIL$' "$W/bootcheck.log")" = 1 ] && grep -q '^boot_hyb_old.adl .*FAIL$' "$W/bootcheck.log" && echo "pass acorncheck(boot)" || fail+=("acorncheck(boot)")
mkdir -p "$W/shots"; s=$W/t/s; mkdir -p "$s"; cp "$T/blank40.dsd" "$T/adfs_l.adl" "$T/pc720.img" "$T/f.hybrid" "$T/g.a800" "$s/"
timeout 120 "$B/screenshot/screenshot" "$W/shots" "$s/blank40.dsd" "$s/adfs_l.adl" "$s/pc720.img" "$s/f.hybrid" "$s/g.a800" >"$W/shots.log" 2>&1 \
  && echo "pass screenshot ($(ls "$W/shots" | wc -l) PNGs)" || fail+=(screenshot)
# X11 on Xvfb, even when run from a Wayland desktop (Qt would otherwise open its windows there)
mkdir -p "$W/xd"; (export QT_QPA_PLATFORM=xcb; unset WAYLAND_DISPLAY; timeout 300 xvfb-run -a -s "-screen 0 1280x800x24" "$B/xdragtest/xdragtest" "$W/xd" >"$W/xdrag.log" 2>&1) \
  && grep -q "ALL OK" "$W/xdrag.log" && echo "pass xdragtest" || fail+=(xdragtest)
rm -rf "$W/t"
[ ${#fail[@]} -eq 0 ] && { echo "ALL PASSED"; exit 0; } || { echo "FAILED: ${fail[*]}"; exit 1; }
