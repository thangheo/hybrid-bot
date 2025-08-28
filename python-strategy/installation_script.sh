#!/bin/bash
# ================================================================
# Markov Persistence Installation Script
# File: scripts/install_persistence.sh
# ================================================================

set -e  # Exit on any error

echo "🚀 Installing Markov Persistence for Trading Bot..."
echo "=" * 60

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "main.py" ] || [ ! -d "src" ]; then
    print_error "Please run this script from the trading bot root directory"
    exit 1
fi

print_status "Detected trading bot directory: $(pwd)"

# 1. BACKUP EXISTING FILES
print_status "Step 1: Creating backup of existing files..."

BACKUP_DIR="backups/persistence_install_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

# Backup critical files
if [ -f "main.py" ]; then
    cp main.py "$BACKUP_DIR/main.py.bak"
    print_success "Backed up main.py"
fi

if [ -f "config/trading_bot.yaml" ]; then
    cp config/trading_bot.yaml "$BACKUP_DIR/trading_bot.yaml.bak"
    print_success "Backed up config file"
fi

if [ -f "src/strategy/unified_manager.py" ]; then
    cp src/strategy/unified_manager.py "$BACKUP_DIR/unified_manager.py.bak"
    print_success "Backed up unified manager"
fi

print_success "Backup completed in $BACKUP_DIR"

# 2. CREATE REQUIRED DIRECTORIES
print_status "Step 2: Creating required directories..."

mkdir -p data
mkdir -p logs
mkdir -p src/strategy/strategies

print_success "Directories created"

# 3. INSTALL PYTHON DEPENDENCIES
print_status "Step 3: Installing additional Python dependencies..."

# Check if we have a virtual environment
if [ -n "$VIRTUAL_ENV" ]; then
    print_success "Using virtual environment: $VIRTUAL_ENV"
else
    print_warning "No virtual environment detected. Consider using one."
fi

# Install SQLite3 (should be built-in, but check)
python3 -c "import sqlite3; print('✅ SQLite3 available')" || {
    print_error "SQLite3 not available in Python"
    exit 1
}

# Install additional dependencies if needed
pip install --upgrade scipy > /dev/null 2>&1
print_success "Dependencies verified"

# 4. CREATE PERSISTENT MARKOV STRATEGY FILE
print_status "Step 4: Creating persistent Markov strategy..."

# This would normally copy the file we created in the artifact
# For now, we'll create a placeholder that tells user to copy the code

cat > src/strategy/strategies/markov_etf_strategy_persistent.py << 'EOF'
# ================================================================
# Markov ETF Strategy with Persistence - PLACEHOLDER
# ================================================================

# TODO: Copy the complete implementation from the artifact
# This file should contain the MarkovPersistenceManager and 
# MarkovETFStrategyPersistent classes

print("⚠️ Please copy the complete persistent strategy implementation")
print("   from the provided artifact to this file")

# Temporary import to prevent errors
from .markov_etf_strategy import MarkovETFStrategy as MarkovETFStrategyPersistent
EOF

print_warning "Created placeholder file - you need to copy the full implementation"

# 5. UPDATE CONFIG FILE
print_status "Step 5: Updating configuration file..."

if [ -f "config/trading_bot.yaml" ]; then
    # Add persistence config if not exists
    if ! grep -q "markov_persistence:" config/trading_bot.yaml; then
        print_status "Adding persistence configuration..."
        
        cat >> config/trading_bot.yaml << 'EOF'

# 🗄️ MARKOV PERSISTENCE CONFIGURATION (Added by install script)
markov_persistence:
  enabled: true
  db_path: "data/markov_persistence.db"
  
  # Save settings
  save_frequency: 10          # Save every N signals
  auto_load_on_init: true     # Auto load on startup
  max_history_age_hours: 168  # Keep history for 1 week
  
  # Cleanup settings
  max_history_entries: 1000   # Max entries per symbol
  cleanup_frequency: 100      # Cleanup every N saves
  
  # Backup settings
  backup_enabled: true
  backup_frequency: 24        # Backup every 24 hours
  backup_retention_days: 30   # Keep backups for 30 days
EOF
        print_success "Added persistence configuration"
    else
        print_success "Persistence configuration already exists"
    fi
else
    print_error "Config file not found: config/trading_bot.yaml"
    exit 1
fi

# 6. CREATE DATABASE DIRECTORY AND SET PERMISSIONS
print_status "Step 6: Setting up database directory..."

# Create data directory with proper permissions
mkdir -p data
chmod 755 data

# Create logs directory
mkdir -p logs
chmod 755 logs

print_success "Database directory ready"

# 7. CREATE TESTING SCRIPT
print_status "Step 7: Creating test script..."

cat > scripts/test_persistence.py << 'EOF'
#!/usr/bin/env python3
"""
Quick persistence test script
"""
import sys
import os
import asyncio

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

async def test_basic_persistence():
    """Test basic persistence functionality"""
    try:
        from strategy.strategies.markov_etf_strategy_persistent import test_markov_persistence
        await test_markov_persistence()
        print("✅ Persistence test completed successfully!")
    except ImportError as e:
        print(f"❌ Import error: {e}")
        print("Please ensure the persistent strategy file is properly implemented")
    except Exception as e:
        print(f"❌ Test failed: {e}")

if __name__ == "__main__":
    asyncio.run(test_basic_persistence())
EOF

chmod +x scripts/test_persistence.py
print_success "Test script created: scripts/test_persistence.py"

# 8. CREATE PERSISTENCE UTILITIES
print_status "Step 8: Creating utility scripts..."

# Database viewer script
cat > scripts/view_persistence_db.py << 'EOF'
#!/usr/bin/env python3
"""
View persistence database contents
"""
import sqlite3
import sys
from pathlib import Path

def view_database(db_path="data/markov_persistence.db"):
    """View database contents"""
    if not Path(db_path).exists():
        print(f"❌ Database not found: {db_path}")
        return
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    try:
        # Show tables
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
        tables = cursor.fetchall()
        
        print(f"📊 Database: {db_path}")
        print(f"📋 Tables: {len(tables)}")
        
        for table_name, in tables:
            cursor.execute(f"SELECT COUNT(*) FROM {table_name}")
            count = cursor.fetchone()[0]
            print(f"   {table_name}: {count} records")
        
        # Show recent transitions
        print(f"\n🔄 Recent Transitions:")
        cursor.execute("""
            SELECT symbol, from_regime, to_regime, probability, updated_at
            FROM transition_matrix 
            ORDER BY updated_at DESC 
            LIMIT 5
        """)
        
        for row in cursor.fetchall():
            print(f"   {row[0]}: {row[1]} → {row[2]} ({row[3]:.3f})")
            
    except Exception as e:
        print(f"❌ Error reading database: {e}")
    finally:
        conn.close()

if __name__ == "__main__":
    db_path = sys.argv[1] if len(sys.argv) > 1 else "data/markov_persistence.db"
    view_database(db_path)
EOF

chmod +x scripts/view_persistence_db.py
print_success "Database viewer created: scripts/view_persistence_db.py"

# 9. CREATE PERSISTENCE MANAGEMENT SCRIPT
cat > scripts/manage_persistence.sh << 'EOF'
#!/bin/bash
# Persistence management utility

case "$1" in
    "backup")
        echo "💾 Creating persistence backup..."
        if [ -f "data/markov_persistence.db" ]; then
            backup_file="backups/markov_persistence_$(date +%Y%m%d_%H%M%S).db"
            mkdir -p backups
            cp data/markov_persistence.db "$backup_file"
            echo "✅ Backup created: $backup_file"
        else
            echo "❌ No persistence database found"
        fi
        ;;
    "restore")
        if [ -z "$2" ]; then
            echo "Usage: $0 restore <backup_file>"
            exit 1
        fi
        echo "📂 Restoring from backup: $2"
        cp "$2" data/markov_persistence.db
        echo "✅ Database restored"
        ;;
    "stats")
        python3 scripts/view_persistence_db.py
        ;;
    "clean")
        echo "🧹 Cleaning old persistence data..."
        if [ -f "data/markov_persistence.db" ]; then
            sqlite3 data/markov_persistence.db "DELETE FROM regime_history WHERE timestamp < datetime('now', '-7 days')"
            echo "✅ Cleaned old regime history"
        fi
        ;;
    *)
        echo "Usage: $0 {backup|restore|stats|clean}"
        echo "Commands:"
        echo "  backup  - Create backup of persistence database"
        echo "  restore - Restore from backup file"
        echo "  stats   - Show database statistics"
        echo "  clean   - Clean old data"
        ;;
esac
EOF

chmod +x scripts/manage_persistence.sh
print_success "Management script created: scripts/manage_persistence.sh"

# 10. FINAL SETUP AND VERIFICATION
print_status "Step 10: Final verification..."

# Check file structure
expected_files=(
    "src/strategy/strategies/markov_etf_strategy_persistent.py"
    "scripts/test_persistence.py"
    "scripts/view_persistence_db.py"
    "scripts/manage_persistence.sh"
    "data"
    "logs"
)

all_good=true
for file in "${expected_files[@]}"; do
    if [ -e "$file" ]; then
        print_success "✅ $file"
    else
        print_error "❌ $file missing"
        all_good=false
    fi
done

# Show installation summary
echo ""
echo "🎉 INSTALLATION SUMMARY"
echo "=" * 40

if [ "$all_good" = true ]; then
    print_success "Installation completed successfully!"
else
    print_warning "Installation completed with warnings"
fi

echo ""
echo "📋 NEXT STEPS:"
echo "1. Copy the complete MarkovETFStrategyPersistent implementation"
echo "   to: src/strategy/strategies/markov_etf_strategy_persistent.py"
echo ""
echo "2. Update your main.py to use EnhancedTradingBotWithPersistence"
echo ""
echo "3. Test the persistence functionality:"
echo "   python3 scripts/test_persistence.py"
echo ""
echo "4. Run the bot with persistence enabled:"
echo "   python3 main.py"
echo ""
echo "5. Monitor persistence with utilities:"
echo "   ./scripts/manage_persistence.sh stats"
echo ""

print_success "Installation script completed!"
print_status "Backup location: $BACKUP_DIR"