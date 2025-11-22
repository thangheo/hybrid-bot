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
    message= '''
Vấn đề **không nằm ở script**, mà nằm ở cấu hình PostgreSQL:
**PostgreSQL của bạn không chấp nhận đăng nhập user `postgres` qua password**, nên mọi kết nối TCP như:

```
postgresql://postgres:Pass_1234@localhost:5432/postgres
```

đều sẽ thất bại.

Đây là lỗi kinh điển: **pg_hba.conf đang set authentication mode = peer**, khiến PostgreSQL bỏ qua password và yêu cầu user Linux phải là `postgres`.

---

# 1. Xác minh lỗi

Chạy:

```
sudo -u postgres psql -c "SELECT 1;"
```

→ Thành công.

Nhưng chạy:

```
psql -U postgres -h localhost
```

→ Bạn sẽ thấy:

```
FATAL: password authentication failed for user "postgres"
```

---

# 2. Mở file pg_hba.conf và sửa đúng dòng

Chạy:

```
sudo nano /etc/postgresql/*/main/pg_hba.conf
```

Tìm các dòng:

```
local   all             postgres                                peer
local   all             all                                     peer
```

**Sửa thành:**

```
local   all             postgres                                md5
local   all             all                                     md5
```

Nếu bạn có dòng IPv4:

```
host    all             all             127.0.0.1/32            scram-sha-256
```

-> giữ nguyên hoặc đổi thành `md5` tùy bạn.

---

# 3. Restart PostgreSQL

```
sudo systemctl restart postgresql
```

---

# 4. Set password thật sự cho user postgres

Login bằng UNIX socket:

```
sudo -u postgres psql
```

Trong psql:

```
ALTER USER postgres WITH PASSWORD 'Pass_1234';
\q
```

---

# 5. Test lại kết nối TCP

```
psql "postgresql://postgres:Pass_1234@localhost:5432/postgres"
```

Nếu dòng này chạy được → script của bạn sẽ chạy được.

---

# Tại sao script thất bại?

psycopg2 kết nối qua TCP, nên bị chặn bởi cấu hình:

```
local all postgres peer
```

Peer = verify identity by matching Linux username → password bị bỏ qua → luôn fail.

---

# Nếu bạn muốn script hoạt động mà không phải sửa cấu hình?

Thì đổi code kết nối thành **UNIX socket**, không dùng password:

Thay:

```
conn = psycopg2.connect(DATABASE_URL)
```

bằng:

```
conn = psycopg2.connect(
    dbname="postgres",
    user="postgres",
    host="/var/run/postgresql/"
)
```

Lưu ý: đường dẫn socket đúng trên Ubuntu.

---

# Muốn tôi viết bản setup_postgres.py tự động detect mọi môi trường và luôn chạy thành công?

Tôi có thể viết bản nâng cấp hoàn chỉnh.
'''
    print(message)
if __name__ == "__main__":
    setup_postgres()
