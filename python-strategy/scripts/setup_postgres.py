
# scripts/setup_postgres.py
"""
PostgreSQL setup script
Run this to create database and user if they don't exist
"""
import psycopg2
from psycopg2.extensions import ISOLATION_LEVEL_AUTOCOMMIT
import os

def setup_postgres():
    """Setup PostgreSQL database and user"""
    
    # Connection to postgres (default database)
    try:
        # Try to connect as postgres user first
        # get url from environment variable or use default
        print("🔧 Setting up PostgreSQL database and user...")
        DATABASE_URL = os.getenv("DATABASE_URL", "postgresql://postgres:Pass_1234@localhost:5432/postgres")
        conn = psycopg2.connect(DATABASE_URL)
        conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
        cursor = conn.cursor()
        
        # Create trading user if not exists
        try:
            cursor.execute("CREATE USER trading WITH PASSWORD 'Pass_1234';")
            print("✓ Created trading user")
        except psycopg2.errors.DuplicateObject:
            print("✓ Trading user already exists")
        
        # Create trading database if not exists
        try:
            cursor.execute("CREATE DATABASE trading_db OWNER trading;")
            print("✓ Created trading_db database")
        except psycopg2.errors.DuplicateDatabase:
            print("✓ trading_db database already exists")
        
        # Grant privileges
        cursor.execute("GRANT ALL PRIVILEGES ON DATABASE trading_db TO trading;")
        print("✓ Granted privileges to trading user")
        
        conn.close()
        
        # Test connection with trading user
        test_conn = psycopg2.connect(
            host="localhost",
            database="trading_db",
            user="trading",
            port=5432,
            password="Pass_1234"
        )
        test_conn.close()
        print("✓ Connection test successful")
        
    except Exception as e:
        print(f"❌ Setup failed: {e}")
        print("\nTry running these commands manually as postgres user:")
        print("sudo -u postgres psql")
        print("CREATE USER trading WITH PASSWORD 'Pass_1234';")
        print("CREATE DATABASE trading_db OWNER trading;")
        print("GRANT ALL PRIVILEGES ON DATABASE trading_db TO trading;")
        print("\\q")

if __name__ == "__main__":
    setup_postgres()
