import psycopg2
from psycopg2.extensions import ISOLATION_LEVEL_AUTOCOMMIT
import os

def setup_postgres():
    print("Setting up PostgreSQL...")

    # safer: do not force password if using peer
    try:
        conn = psycopg2.connect(
            dbname="postgres",
            user="postgres",
            host="/var/run/postgresql/"
        )
    except:
        # fallback TCP
        DATABASE_URL = os.getenv(
            "DATABASE_URL",
            "postgresql://postgres:Pass_1234@localhost:5432/postgres"
        )
        conn = psycopg2.connect(DATABASE_URL)

    conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
    cur = conn.cursor()

    # create role
    try:
        cur.execute("CREATE USER trading WITH PASSWORD 'Pass_1234';")
        print("✓ Created role trading")
    except psycopg2.Error as e:
        conn.rollback()
        if e.pgcode == psycopg2.errorcodes.DUPLICATE_OBJECT:
            print("✓ Role trading exists")
        else:
            raise

    # create db
    try:
        cur.execute("CREATE DATABASE trading_db OWNER trading;")
        print("✓ Created DB trading_db")
    except psycopg2.Error as e:
        conn.rollback()
        if e.pgcode == psycopg2.errorcodes.DUPLICATE_DATABASE:
            print("✓ Database trading_db exists")
        else:
            raise

    cur.execute("GRANT ALL PRIVILEGES ON DATABASE trading_db TO trading;")
    print("✓ Granted privileges")

    conn.close()
    print("✓ Done")

if __name__ == "__main__":
    setup_postgres()
