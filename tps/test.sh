for ((n=0;n<100;n++))
do
    meson setup build > /dev/null
    meson compile -C build > /dev/null
    meson test -C build > /dev/null
    tests/ps_compare.py build/tps
done