#!/usr/bin/env python3
# ================================================================
# run.py - Easy startup script for the trading bot
# Handles common operations with simple commands
# ================================================================

import asyncio
import sys
import os
import subprocess
from pathlib import Path
import argparse

# Add src to path
sys.path.insert(0, str(Path(__file__).parent / 'src'))

def print_banner():
    """Print startup banner"""
    print("""
╔══════════════════════════════════════════════════════════════╗
║                    🚀 CRYPTO TRADING BOT 🚀                    ║
║                                                              ║
║  Daily timeframe statistical trading system                  ║
║  Built with CCXT, SQLAlchemy, and modern Python libraries   ║
╚══════════════════════════════════════════════════════════════╝
    """)

def check_dependencies():
    """Check if required dependencies are installed"""
    print("🔍 Checking dependencies...")
    
    required_packages = [
        'ccxt', 'pandas', 'numpy', 'sqlalchemy', 
        'aiohttp', 'apscheduler', 'pyyaml', 'python-dotenv'
    ]
    
    missing = []
    for package in required_packages:
        try:
            __import__(package.replace('-', '_'))
            print(f"   ✓ {package}")
        except ImportError:
            print(f"   ❌ {package} - MISSING")
            missing.append(package)
    
    if missing:
        print(f"\n⚠️  Missing packages: {', '.join(missing)}")
        print("Install with: pip install " + " ".join(missing))
        return False
    
    print("✅ All dependencies satisfied")
    return True

def check_configuration():
    """Check if configuration files exist"""
    print("\n🔧 Checking configuration...")
    
    config_files = [
        'config/trading_bot.yaml',
        '.env'
    ]
    
    all_exist = True
    for config_file in config_files:
        if Path(config_file).exists():
            print(f"   ✓ {config_file}")
        else:
            print(f"   ❌ {config_file} - MISSING")
            all_exist = False
    
    if not all_exist:
        print("\n⚠️  Missing configuration files")
        print("Run: python run.py --setup")
        return False
    
    print("✅ Configuration files found")
    return True

def setup_configuration():
    """Create basic configuration files"""
    print("🛠️  Setting up configuration...")
    
    # Create config directory
    os.makedirs('config', exist_ok=True)
    
    # Create basic trading_bot.yaml
    config_content = """# Basic Trading Bot Configuration
data_ingestion:
  binance:
    api_key: ${BINANCE_API_KEY}
    api_secret: ${BINANCE_API_SECRET}
    testnet: false
  database:
    url: ${DATABASE_URL}
  symbols:
    - "BTC/USDT"
    - "ETH/USDT"
  timeframes:
    - "1d"

strategy:
  active_strategies:
    - name: "statistical_mean_reversion"
      weight: 1.0
      params:
        zscore_window: 20
        zscore_entry: 2.0
        zscore_exit: 0.5
        ema_short: 10
        ema_long: 30

risk_management:
  max_portfolio_risk: 0.02
  max_position_risk: 0.01
  position_sizing:
    method: "volatility_adjusted"
    base_risk_per_trade: 0.01
    max_position_size: 0.1

order_execution:
  binance:
    api_key: ${BINANCE_API_KEY}
    api_secret: ${BINANCE_API_SECRET}
    testnet: false
  max_slippage: 0.005

portfolio:
  initial_capital: 100000
  base_currency: "USDT"

scheduling:
  signal_interval: 3600
  data_interval: 300

monitoring:
  memory_threshold: 50
  cpu_threshold: 70

alerts:
  slack:
    enabled: false
  email:
    enabled: false

logging:
  level: "INFO"
  file: "logs/trading_bot.log"
"""
    
    with open('config/trading_bot.yaml', 'w') as f:
        f.write(config_content)
    
    # Create .env template
    env_content = """# Trading Bot Environment Variables
# Fill in your actual values

# Binance API credentials (from https://www.binance.com/en/my/settings/api-management)
BINANCE_API_KEY=your_api_key_here
BINANCE_API_SECRET=your_api_secret_here

# Database (SQLite for development, PostgreSQL for production)
DATABASE_URL=sqlite:///data/trading.db

# Optional: Alerts
SLACK_WEBHOOK_URL=https://hooks.slack.com/services/YOUR/SLACK/WEBHOOK
EMAIL_USERNAME=your_email@gmail.com
EMAIL_PASSWORD=your_app_password
"""
    
    with open('.env', 'w') as f:
        f.write(env_content)
    
    # Create logs and data directories
    os.makedirs('logs', exist_ok=True)
    os.makedirs('data', exist_ok=True)
    
    print("✅ Configuration files created")
    print("\n📝 Next steps:")
    print("1. Edit .env file with your API keys")
    print("2. Customize config/trading_bot.yaml as needed")
    print("3. Run: python run.py --test")

async def run_tests():
    """Run integration tests"""
    print("🧪 Running integration tests...")
    
    try:
        from test_integration import IntegrationTester
        tester = IntegrationTester()
        success = await tester.run_quick_test()
        
        if success:
            print("✅ Tests passed - system is ready!")
        else:
            print("❌ Some tests failed - check configuration")
            return False
    except ImportError:
        print("⚠️  Integration test module not found")
        print("Running basic checks instead...")
        
        # Basic check
        if check_dependencies() and check_configuration():
            print("✅ Basic checks passed")
        else:
            return False
    
    return True

async def run_backtest(symbols=None, days=30):
    """Run backtest mode"""
    print(f"📊 Running backtest for {days} days...")
    
    if not symbols:
        symbols = ['BTC/USDT']
    
    try:
        from main_old import run_backtest
        await run_backtest('config/trading_bot.yaml', symbols, days)
    except Exception as e:
        print(f"❌ Backtest failed: {e}")
        return False
    
    return True

async def run_live():
    """Run live trading"""
    print("🔴 Starting LIVE trading mode...")
    print("⚠️  Make sure you're ready for real trading!")
    
    # Additional safety check
    from dotenv import load_dotenv
    load_dotenv()
    
    if os.getenv('BINANCE_TESTNET', 'true').lower() == 'true':
        print("✅ Using testnet - safe for testing")
    else:
        confirm = input("⚠️  PRODUCTION MODE - Are you sure? (yes/no): ")
        if confirm.lower() != 'yes':
            print("Cancelled.")
            return
    
    try:
        from main_old import TradingBot
        bot = TradingBot('config/trading_bot.yaml')
        await bot.start()
    except KeyboardInterrupt:
        print("\n🛑 Trading stopped by user")
    except Exception as e:
        print(f"❌ Trading failed: {e}")

def show_status():
    """Show system status"""
    print("📊 System Status")
    print("=" * 30)
    
    # Check dependencies
    deps_ok = check_dependencies()
    
    # Check configuration
    config_ok = check_configuration()
    
    # Check API keys
    from dotenv import load_dotenv
    load_dotenv()
    
    has_keys = bool(os.getenv('BINANCE_API_KEY')) and bool(os.getenv('BINANCE_API_SECRET'))
    print(f"\n🔑 API Keys: {'✓' if has_keys else '❌ Missing'}")
    
    # Check database
    db_url = os.getenv('DATABASE_URL', 'sqlite:///data/trading.db')
    print(f"🗄️  Database: {db_url}")
    
    # Overall status
    ready = deps_ok and config_ok and has_keys
    print(f"\n🚦 Status: {'✅ READY' if ready else '❌ NOT READY'}")
    
    if not ready:
        print("\n🔧 To fix issues:")
        if not deps_ok:
            print("   - Install missing dependencies")
        if not config_ok:
            print("   - Run: python run.py --setup")
        if not has_keys:
            print("   - Add API keys to .env file")

def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='Crypto Trading Bot')
    
    parser.add_argument('--setup', action='store_true',
                       help='Setup configuration files')
    parser.add_argument('--test', action='store_true',
                       help='Run integration tests')
    parser.add_argument('--backtest', action='store_true',
                       help='Run backtest mode')
    parser.add_argument('--live', action='store_true',
                       help='Run live trading')
    parser.add_argument('--status', action='store_true',
                       help='Show system status')
    parser.add_argument('--symbols', nargs='+', default=['BTC/USDT'],
                       help='Symbols for backtesting')
    parser.add_argument('--days', type=int, default=30,
                       help='Days for backtesting')
    
    args = parser.parse_args()
    
    print_banner()
    
    if args.setup:
        setup_configuration()
    elif args.test:
        asyncio.run(run_tests())
    elif args.backtest:
        asyncio.run(run_backtest(args.symbols, args.days))
    elif args.live:
        asyncio.run(run_live())
    elif args.status:
        show_status()
    else:
        # Default: show status and help
        show_status()
        print("\n" + "=" * 50)
        print("🚀 QUICK START:")
        print("=" * 50)
        print("1. Setup:    python run.py --setup")
        print("2. Test:     python run.py --test")
        print("3. Backtest: python run.py --backtest")
        print("4. Live:     python run.py --live")
        print("\n📚 More options: python run.py --help")

if __name__ == "__main__":
    main()