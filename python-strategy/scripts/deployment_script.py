#!/usr/bin/env python3
# ================================================================
# deploy.py - Complete Trading Bot Deployment
# Handles database setup, configuration, and service installation
# ================================================================

import os
import sys
import subprocess
import shutil
from pathlib import Path
import yaml
import psycopg2
from psycopg2.extensions import ISOLATION_LEVEL_AUTOCOMMIT
import asyncio
import logging

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class TradingBotDeployer:
    """Handles complete deployment of the trading bot"""
    
    def __init__(self, deployment_path="/opt/crypto-trading-bot"):
        self.deployment_path = Path(deployment_path)
        self.current_path = Path(__file__).parent
        self.venv_path = self.deployment_path / "venv"
        
    def deploy(self, production=False):
        """Complete deployment process"""
        logger.info("🚀 Starting Trading Bot Deployment")
        logger.info("=" * 50)
        
        try:
            # Step 1: System preparation
            self.prepare_system()
            
            # Step 2: Create deployment directory
            self.setup_directories()
            
            # Step 3: Copy files
            self.copy_files()
            
            # Step 4: Setup Python environment
            self.setup_python_environment()
            
            # Step 5: Setup database
            self.setup_database()
            
            # Step 6: Create configuration
            self.create_configuration(production)
            
            # Step 7: Setup systemd service
            if production:
                self.setup_systemd_service()
            
            # Step 8: Test installation
            self.test_installation()
            
            logger.info("🎉 Deployment completed successfully!")
            self.show_next_steps(production)
            
        except Exception as e:
            logger.error(f"❌ Deployment failed: {e}")
            raise
    
    def prepare_system(self):
        """Prepare the system for deployment"""
        logger.info("1. Preparing system...")
        
        # Update package list
        subprocess.run(['sudo', 'apt', 'update'], check=True)
        
        # Install required system packages
        packages = [
            'python3',
            'python3-venv',
            'python3-pip',
            'postgresql',
            'postgresql-contrib',
            'git',
            'curl',
            'htop'
        ]
        
        subprocess.run(['sudo', 'apt', 'install', '-y'] + packages, check=True)
        logger.info("   ✓ System packages installed")
        
        # Create trading user if not exists
        try:
            subprocess.run(['sudo', 'useradd', '-r', '-s', '/bin/false', 'trading'], 
                         check=True, capture_output=True)
            logger.info("   ✓ Trading user created")
        except subprocess.CalledProcessError:
            logger.info("   ✓ Trading user already exists")
    
    def setup_directories(self):
        """Setup deployment directories"""
        logger.info("2. Setting up directories...")
        
        # Create main directory
        subprocess.run(['sudo', 'mkdir', '-p', str(self.deployment_path)], check=True)
        
        # Create subdirectories
        subdirs = ['src', 'config', 'logs', 'data', 'scripts', 'tests']
        for subdir in subdirs:
            (self.deployment_path / subdir).mkdir(exist_ok=True)
        
        # Set ownership
        subprocess.run(['sudo', 'chown', '-R', 'trading:trading', str(self.deployment_path)], 
                      check=True)
        
        logger.info("   ✓ Directories created")
    
    def copy_files(self):
        """Copy source files to deployment directory"""
        logger.info("3. Copying files...")
        
        # Copy source code
        if (self.current_path / 'src').exists():
            shutil.copytree(self.current_path / 'src', self.deployment_path / 'src', 
                          dirs_exist_ok=True)
        
        # Copy configuration templates
        if (self.current_path / 'config').exists():
            shutil.copytree(self.current_path / 'config', self.deployment_path / 'config',
                          dirs_exist_ok=True)
        
        # Copy requirements
        if (self.current_path / 'requirements.txt').exists():
            shutil.copy2(self.current_path / 'requirements.txt', self.deployment_path)
        
        # Copy main script
        if (self.current_path / 'main.py').exists():
            shutil.copy2(self.current_path / 'main.py', self.deployment_path)
        
        # Set proper ownership
        subprocess.run(['sudo', 'chown', '-R', 'trading:trading', str(self.deployment_path)],
                      check=True)
        
        logger.info("   ✓ Files copied")
    
    def setup_python_environment(self):
        """Setup Python virtual environment and install dependencies"""
        logger.info("4. Setting up Python environment...")
        
        # Create virtual environment
        subprocess.run(['sudo', '-u', 'trading', 'python3', '-m', 'venv', str(self.venv_path)],
                      check=True)
        
        # Install dependencies
        pip_path = self.venv_path / 'bin' / 'pip'
        requirements_file = self.deployment_path / 'requirements.txt'
        
        if requirements_file.exists():
            subprocess.run(['sudo', '-u', 'trading', str(pip_path), 'install', '-r', 
                          str(requirements_file)], check=True)
        else:
            # Install essential packages
            essential_packages = [
                'ccxt>=4.0.0',
                'pandas>=1.3.0',
                'numpy>=1.21.0',
                'sqlalchemy>=1.4.0',
                'asyncpg>=0.24.0',
                'aiohttp>=3.8.0',
                'apscheduler>=3.9.0',
                'pyyaml>=6.0',
                'python-dotenv>=0.19.0',
                'psutil>=5.8.0',
                'ta-lib>=0.4.24',
                'requests>=2.26.0'
            ]
            
            subprocess.run(['sudo', '-u', 'trading', str(pip_path), 'install'] + essential_packages,
                          check=True)
        
        logger.info("   ✓ Python environment setup complete")
    
    def setup_database(self):
        """Setup PostgreSQL database"""
        logger.info("5. Setting up database...")
        
        try:
            # Start PostgreSQL service
            subprocess.run(['sudo', 'systemctl', 'start', 'postgresql'], check=True)
            subprocess.run(['sudo', 'systemctl', 'enable', 'postgresql'], check=True)
            
            # Create database and user
            # Connect as postgres user
            conn = psycopg2.connect(
                host='localhost',
                database='postgres',
                user='postgres',
                port=5432
            )
            conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
            cursor = conn.cursor()
            
            # Create database
            try:
                cursor.execute("CREATE DATABASE trading_db;")
                logger.info("   ✓ Database created")
            except psycopg2.errors.DuplicateDatabase:
                logger.info("   ✓ Database already exists")
            
            # Create user
            try:
                cursor.execute("CREATE USER trading WITH PASSWORD 'Pass_1234';")
                cursor.execute("GRANT ALL PRIVILEGES ON DATABASE trading_db TO trading;")
                logger.info("   ✓ Database user created")
            except psycopg2.errors.DuplicateObject:
                logger.info("   ✓ Database user already exists")
            
            cursor.close()
            conn.close()
            
            # Create tables
            self.create_database_tables()
            
        except Exception as e:
            logger.warning(f"   ⚠️ Database setup had issues: {e}")
            logger.info("   Manual database setup may be required")
    
    def create_database_tables(self):
        """Create database tables"""
        logger.info("6. Creating database tables...")
        
        # SQL for creating tables
        create_tables_sql = """
        -- OHLCV Data Table
        CREATE TABLE IF NOT EXISTS ohlcv_data (
            id SERIAL PRIMARY KEY,
            symbol VARCHAR(20) NOT NULL,
            timeframe VARCHAR(10) NOT NULL,
            timestamp BIGINT NOT NULL,
            open DECIMAL(20,8) NOT NULL,
            high DECIMAL(20,8) NOT NULL,
            low DECIMAL(20,8) NOT NULL,
            close DECIMAL(20,8) NOT NULL,
            volume DECIMAL(20,8) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(symbol, timeframe, timestamp)
        );
        
        -- Trading Pairs Info
        CREATE TABLE IF NOT EXISTS trading_pairs (
            symbol VARCHAR(20) PRIMARY KEY,
            base_asset VARCHAR(10) NOT NULL,
            quote_asset VARCHAR(10) NOT NULL,
            min_qty DECIMAL(20,8),
            max_qty DECIMAL(20,8),
            step_size DECIMAL(20,8),
            is_active BOOLEAN DEFAULT TRUE,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        
        -- Orders Table
        CREATE TABLE IF NOT EXISTS orders (
            id VARCHAR(50) PRIMARY KEY,
            symbol VARCHAR(20) NOT NULL,
            side VARCHAR(10) NOT NULL,
            order_type VARCHAR(20) NOT NULL,
            quantity DECIMAL(20,8) NOT NULL,
            price DECIMAL(20,8),
            status VARCHAR(20) NOT NULL,
            exchange_order_id VARCHAR(50),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        
        -- Portfolio Positions
        CREATE TABLE IF NOT EXISTS positions (
            symbol VARCHAR(20) PRIMARY KEY,
            quantity DECIMAL(20,8) NOT NULL,
            average_price DECIMAL(20,8),
            unrealized_pnl DECIMAL(20,8) DEFAULT 0,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        
        -- Create indexes for performance
        CREATE INDEX IF NOT EXISTS idx_ohlcv_symbol_time ON ohlcv_data(symbol, timeframe, timestamp);
        CREATE INDEX IF NOT EXISTS idx_orders_symbol ON orders(symbol);
        CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
        """
        
        try:
            conn = psycopg2.connect(
                host='localhost',
                database='trading_db',
                user='trading',
                password='Pass_1234',
                port=5432
            )
            cursor = conn.cursor()
            cursor.execute(create_tables_sql)
            conn.commit()
            cursor.close()
            conn.close()
            logger.info("   ✓ Database tables created")
        except Exception as e:
            logger.warning(f"   ⚠️ Table creation failed: {e}")
    
    def create_configuration(self, production=False):
        """Create configuration files"""
        logger.info("7. Creating configuration...")
        
        # Create .env file
        env_file = self.deployment_path / '.env'
        env_content = """# Trading Bot Environment Variables
# Copy this file and fill in your actual values

# Binance API credentials (get from https://www.binance.com/en/my/settings/api-management)
BINANCE_API_KEY=your_api_key_here
BINANCE_API_SECRET=your_api_secret_here

# Database
DATABASE_URL=postgresql://trading:Pass_1234@localhost:5432/trading_db
DB_PASSWORD=Pass_1234

# Use testnet for development/testing
BINANCE_TESTNET=true

# Alert configurations (optional)
SLACK_WEBHOOK_URL=https://hooks.slack.com/services/YOUR/SLACK/WEBHOOK
EMAIL_USERNAME=your_email@gmail.com
EMAIL_PASSWORD=your_app_password

# Telegram (optional)
TELEGRAM_BOT_TOKEN=your_bot_token
TELEGRAM_CHAT_ID=your_chat_id

# Discord (optional)
DISCORD_WEBHOOK_URL=your_discord_webhook
"""
        
        with open(env_file, 'w') as f:
            f.write(env_content)
        
        # Update config for production
        config_file = self.deployment_path / 'config' / 'trading_bot.yaml'
        if config_file.exists():
            with open(config_file, 'r') as f:
                config = yaml.safe_load(f)
            
            if production:
                # Update for production
                config['data_ingestion']['binance']['testnet'] = False
                config['development']['paper_trading'] = False
                config['development']['dry_run'] = False
                config['logging']['level'] = 'INFO'
            else:
                # Keep as development/testing
                config['data_ingestion']['binance']['testnet'] = True
                config['development']['paper_trading'] = True
                config['development']['dry_run'] = True
                config['logging']['level'] = 'DEBUG'
            
            with open(config_file, 'w') as f:
                yaml.dump(config, f, default_flow_style=False, indent=2)
        
        # Set proper ownership
        subprocess.run(['sudo', 'chown', 'trading:trading', str(env_file)], check=True)
        subprocess.run(['sudo', 'chmod', '600', str(env_file)], check=True)  # Secure permissions
        
        logger.info("   ✓ Configuration files created")
    
    def setup_systemd_service(self):
        """Setup systemd service for production"""
        logger.info("8. Setting up systemd service...")
        
        service_content = f"""[Unit]
Description=Crypto Trading Bot
After=network.target postgresql.service
Requires=postgresql.service

[Service]
Type=simple
User=trading
Group=trading
WorkingDirectory={self.deployment_path}
Environment=PYTHONPATH={self.deployment_path}
Environment=CONFIG_PATH={self.deployment_path}/config/trading_bot.yaml
ExecStart={self.venv_path}/bin/python main.py
ExecReload=/bin/kill -HUP $MAINPID
KillMode=mixed
KillSignal=SIGTERM
TimeoutStopSec=30
Restart=always
RestartSec=10

# Security settings
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths={self.deployment_path}/logs {self.deployment_path}/data
NoNewPrivileges=true

# Resource limits
MemoryMax=1G
CPUQuota=200%

[Install]
WantedBy=multi-user.target
"""
        
        service_file = '/etc/systemd/system/crypto-trading-bot.service'
        with open(service_file, 'w') as f:
            f.write(service_content)
        
        # Reload systemd and enable service
        subprocess.run(['sudo', 'systemctl', 'daemon-reload'], check=True)
        subprocess.run(['sudo', 'systemctl', 'enable', 'crypto-trading-bot'], check=True)
        
        logger.info("   ✓ Systemd service configured")
    
    def test_installation(self):
        """Test the installation"""
        logger.info("9. Testing installation...")
        
        # Test Python environment
        python_path = self.venv_path / 'bin' / 'python'
        result = subprocess.run([str(python_path), '-c', 'import ccxt, pandas, sqlalchemy; print("OK")'],
                              capture_output=True, text=True)
        
        if result.returncode == 0:
            logger.info("   ✓ Python environment test passed")
        else:
            logger.error(f"   ❌ Python environment test failed: {result.stderr}")
            return False
        
        # Test database connection
        try:
            conn = psycopg2.connect(
                host='localhost',
                database='trading_db',
                user='trading',
                password='Pass_1234',
                port=5432
            )
            conn.close()
            logger.info("   ✓ Database connection test passed")
        except Exception as e:
            logger.error(f"   ❌ Database connection test failed: {e}")
            return False
        
        logger.info("   ✓ All tests passed")
        return True
    
    def show_next_steps(self, production=False):
        """Show next steps to the user"""
        logger.info("\n📋 Next Steps:")
        logger.info("=" * 30)
        
        logger.info("1. Configure your API keys:")
        logger.info(f"   Edit {self.deployment_path}/.env")
        logger.info("   Add your Binance API key and secret")
        
        logger.info("\n2. Test the configuration:")
        logger.info(f"   cd {self.deployment_path}")
        logger.info("   sudo -u trading ./venv/bin/python main.py --backtest")
        
        if production:
            logger.info("\n3. Start the service:")
            logger.info("   sudo systemctl start crypto-trading-bot")
            logger.info("   sudo systemctl status crypto-trading-bot")
            
            logger.info("\n4. Monitor logs:")
            logger.info("   sudo journalctl -u crypto-trading-bot -f")
            logger.info(f"   tail -f {self.deployment_path}/logs/trading_bot.log")
        else:
            logger.info("\n3. Run in development mode:")
            logger.info(f"   cd {self.deployment_path}")
            logger.info("   sudo -u trading ./venv/bin/python main.py")
        
        logger.info("\n5. Configure alerts (optional):")
        logger.info("   - Slack webhook URL")
        logger.info("   - Email settings")
        logger.info("   - Telegram bot")
        
        logger.info(f"\n🔐 Important Security Notes:")
        logger.info("   - API keys are stored in .env with restricted permissions")
        logger.info("   - Service runs as dedicated 'trading' user")
        logger.info("   - Use testnet for initial testing")
        logger.info("   - Monitor resource usage and logs regularly")

async def main():
    """Main deployment function"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Deploy Crypto Trading Bot')
    parser.add_argument('--production', action='store_true',
                       help='Deploy for production (disable testnet)')
    parser.add_argument('--path', default='/opt/crypto-trading-bot',
                       help='Deployment path')
    
    args = parser.parse_args()
    
    if args.production:
        confirm = input("⚠️  Production deployment will disable testnet. Continue? (y/N): ")
        if confirm.lower() != 'y':
            print("Deployment cancelled.")
            return
    
    deployer = TradingBotDeployer(args.path)
    deployer.deploy(production=args.production)

if __name__ == "__main__":
    asyncio.run(main())