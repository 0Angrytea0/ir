@echo off
setlocal

if not exist out mkdir out

bin\zipf.exe out\tokens out\zipf_raw.tsv out\terms_raw.tsv
bin\zipf.exe out\stem_tokens out\zipf_stem.tsv out\terms_stem.tsv

endlocal
