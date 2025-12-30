@echo off
setlocal

chcp 65001 >nul

if not exist out mkdir out
if not exist out\tokens_ruwiki mkdir out\tokens_ruwiki
if not exist out\tokens_ru_wikisource mkdir out\tokens_ru_wikisource

bin\tokenize.exe corpus\ruwiki\docs out\tokens_ruwiki out\tokens_meta_ruwiki.tsv
bin\tokenize.exe corpus\ru_wikisource\docs out\tokens_ru_wikisource out\tokens_meta_ru_wikisource.tsv

endlocal
