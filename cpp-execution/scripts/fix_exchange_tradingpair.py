#!/usr/bin/env python3
import re
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
changed_files = []

# Patterns
assign_pattern = re.compile(r"(\b\w+\.(exchangeId|tradingPair))\s*=\s*\"([^\"]+)\";")
# e.g., obj.exchangeId = "binance";

json_pattern_exchange = re.compile(r"\{\s*\"exchange_id\"\s*,\s*([a-zA-Z0-9_\.]+)\s*\}")
json_pattern_trading = re.compile(r"\{\s*\"trading_pair\"\s*,\s*([a-zA-Z0-9_\.]+)\s*\}")
# e.g., {"exchange_id", exchangeId}

# Also patterns inside initializer lists spanning multiple entries: replace , {"exchange_id", var},
json_pattern_exchange2 = re.compile(r"\{\s*\"exchange_id\"\s*,\s*([a-zA-Z0-9_\.]+)\s*\}")
json_pattern_trading2 = re.compile(r"\{\s*\"trading_pair\"\s*,\s*([a-zA-Z0-9_\.]+)\s*\}")

for path in root.rglob('tests/**/*.cpp'):
    text = path.read_text()
    orig = text
    # Replace assignments
    def assign_repl(m):
        lhs = m.group(1)
        which = m.group(2)
        val = m.group(3)
        if which == 'exchangeId':
            return f"{lhs} = ExchangeId::fromString(\"{val}\");"
        else:
            return f"{lhs} = TradingPair::fromString(\"{val}\");"
    text = assign_pattern.sub(assign_repl, text)
    # Replace json simple patterns
    text = json_pattern_exchange.sub(r'{"exchange_id", \1.toString()}', text)
    text = json_pattern_trading.sub(r'{"trading_pair", \1.toString()}', text)

    if text != orig:
        path.write_text(text)
        changed_files.append(str(path.relative_to(root)))

# Report
print('Modified files:')
for f in changed_files:
    print(f)

if not changed_files:
    print('No files changed.')
