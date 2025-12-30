@echo off
setlocal

if not exist out mkdir out
if not exist out\stem_tokens_ruwiki mkdir out\stem_tokens_ruwiki
if not exist out\stem_tokens_wikisource mkdir out\stem_tokens_wikisource

bin\tokenize.exe --stem corpus\ruwiki\docs out\stem_tokens_ruwiki out\stem_tokens_ruwiki_meta.tsv
bin\tokenize.exe --stem corpus\ru_wikisource\docs out\stem_tokens_wikisource out\stem_tokens_wikisource_meta.tsv

endlocal
