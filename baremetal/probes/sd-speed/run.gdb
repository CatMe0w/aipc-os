set confirm off
set pagination off
file build/sd-speed.elf
load
break probe_done
jump _start
printf "c1=0x%08x raw_lba=%u raw_sectors=%u file_size=%u file_runs=%u\n", entry_c1, raw_lba_used, params.raw_sectors, file_size, file_runs
printf "%-16s ic  rc     init_us first_us single_us   hs_us    work_us    bytes   kB/s mismatch   clock      got     want  nbad\n", "phase"
set $i = 0
while $i < nrec
  set $r = recs[$i]
  set $kbps = $r.work ? (unsigned long long)$r.bytes * 12000000 / 1024 / $r.work : 0
  printf "%-16s %2u %3d %11u %8u %9u %7u %10u %8u %6u %8x %08x %08x %08x %5u\n", $r.name, $r.icache, $r.rc, $r.init / 12, $r.first / 12, $r.single / 12, $r.hs / 12, $r.work / 12, $r.bytes, (unsigned)$kbps, $r.mismatch, $r.clock, $r.got, $r.want, $r.nbad
  set $i = $i + 1
end
