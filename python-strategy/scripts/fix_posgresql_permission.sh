# Connect as postgres superuser
sudo -u postgres psql

# Grant all necessary permissions to trading user
GRANT ALL PRIVILEGES ON DATABASE trading_db TO trading;
GRANT ALL ON SCHEMA public TO trading;
GRANT CREATE ON SCHEMA public TO trading;
GRANT USAGE ON SCHEMA public TO trading;

# Make trading user owner of the database (recommended)
ALTER DATABASE trading_db OWNER TO trading;

# Exit postgres
\q