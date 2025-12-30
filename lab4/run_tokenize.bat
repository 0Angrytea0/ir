@echo off
setlocal

if not exist out mkdir out
if not exist out\tokens_ruwiki mkdir out\tokens_ruwiki
if not exist out\tokens_wikisource mkdir out\tokens_wikisource

bin\tokenize.exe corpus\ruwiki\docs out\tokens_ruwiki out\tokens_ruwiki_meta.tsv
bin\tokenize.exe corpus\ru_wikisource\docs out\tokens_wikisource out\tokens_wikisource_meta.tsv

endlocal
