#!/bin/bash
echo "🧪 Testing Persistence Integration..."

# Test imports
echo "1. Testing imports..."
python3 -c "
import sys
sys.path.insert(0, 'src')
try:
    from strategy.persistence_addon import PersistenceAddon
    print('✅ PersistenceAddon import OK')
except Exception as e:
    print(f'❌ PersistenceAddon import failed: {e}')

try:
    from strategy.strategies.markov_etf_strategy_persistent import MarkovETFStrategyPersistent
    print('✅ Persistent strategy import OK')
except Exception as e:
    print(f'⚠️ Persistent strategy import failed: {e}')
    print('   (This is expected with placeholder - copy full implementation)')
"

# Test config
echo "2. Testing config..."
python3 -c "
import yaml
try:
    with open('config/trading_bot.yaml', 'r') as f:
        config = yaml.safe_load(f)
    
    if 'markov_persistence' in config:
        print('✅ Persistence config found')
        print(f'   Enabled: {config[\"markov_persistence\"].get(\"enabled\", False)}')
        print(f'   DB path: {config[\"markov_persistence\"].get(\"db_path\", \"not set\")}')
    else:
        print('❌ Persistence config missing')
except Exception as e:
    print(f'❌ Config test failed: {e}')
"

# Test main bot capabilities
echo "3. Testing bot capabilities..."
python3 -c "
import sys, os
sys.path.insert(0, 'src')

try:
    import asyncio
    from main import EnhancedTradingBot
    
    async def test_capabilities():
        try:
            bot = EnhancedTradingBot('config/trading_bot.yaml')
            await bot.initialize()
            
            capabilities = bot.strategy_manager.get_system_capabilities()
            persistence = capabilities.get('persistence', {})
            
            print(f'✅ Bot initialized successfully')
            print(f'   Persistence enabled: {persistence.get(\"enabled\", False)}')
            
            if persistence.get('enabled'):
                stats = persistence.get('database_stats', {})
                print(f'   Database ready: {\"database_path\" in persistence}')
        except Exception as e:
            print(f'❌ Bot test failed: {e}')
    
    asyncio.run(test_capabilities())
except Exception as e:
    print(f'❌ Bot import failed: {e}')
"

echo "🏁 Test completed!"
