import psycopg2
import os
from dotenv import load_dotenv

load_dotenv()

def test_db_connection():
    """Test database connection and show status"""
    
    # Test different common configurations
    configs = [
        "postgresql://trading:Pass_1234@localhost:5432/trading_db",  # Default port
        "postgresql://trading:Pass_1234@localhost:5432/trading_db",  # Your port
        "postgresql://postgres:Pass_1234@localhost:5432/postgres",  # As postgres user
    ]
    
    for db_url in configs:
        try:
            print(f"Testing: {db_url}")
            conn = psycopg2.connect(db_url)
            cursor = conn.cursor()
            
            # Check if we can query
            cursor.execute("SELECT version();")
            version = cursor.fetchone()[0]
            print(f"  ✓ Connected! PostgreSQL version: {version[:50]}...")
            
            # List databases
            cursor.execute("SELECT datname FROM pg_database WHERE datistemplate = false;")
            databases = [row[0] for row in cursor.fetchall()]
            print(f"  ✓ Databases: {databases}")
            
            # If this is trading_db, list tables
            if 'trading_db' in db_url:
                cursor.execute("SELECT tablename FROM pg_tables WHERE schemaname = 'public';")
                tables = [row[0] for row in cursor.fetchall()]
                print(f"  ✓ Tables in trading_db: {tables}")
            
            conn.close()
            print("  ✓ Connection successful!\n")
            return db_url
            
        except Exception as e:
            print(f"  ✗ Failed: {e}\n")
    
    return None

if __name__ == "__main__":
    working_url = test_db_connection()
    if working_url:
        print(f"✅ Use this DATABASE_URL: {working_url}")
    else:
        print("❌ No working database connection found")
        print("\nTry these steps:")
        print("1. sudo systemctl status postgresql")
        print("2. sudo systemctl start postgresql") 
        print("3. sudo -u postgres psql -c 'CREATE USER trading WITH PASSWORD \"Pass_1234\";'")
        print("4. sudo -u postgres createdb -O trading trading_db")