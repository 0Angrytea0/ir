@echo off
setlocal

if not exist index mkdir index

bin\indexer.exe --add out\stem_tokens_ruwiki corpus\ruwiki\meta.tsv --add out\stem_tokens_wikisource corpus\ru_wikisource\meta.tsv index\index.bin

endlocal
