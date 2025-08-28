from fastapi import FastAPI, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
import logging
from datetime import datetime
import uuid

# Import routers
from python.monitoring.api.health_endpoints import router as health_router
from python.monitoring.api.metrics_endpoints import router as metrics_router
from python.monitoring.api.alerts_endpoints import router as alerts_router
from python.monitoring.api.trading_endpoints import router as trading_router

# Import config
from python.monitoring.config.settings import Settings
from python.monitoring.utils.security import get_current_user, User

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
)
logger = logging.getLogger(__name__)

# Load settings
settings = Settings()

# Create the FastAPI application
app = FastAPI(
    title="Trading System Monitoring API",
    description="API for monitoring the performance and health of the trading system",
    version="1.0.0",
)

# Configure CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Request ID middleware
@app.middleware("http")
async def add_request_id(request, call_next):
    request_id = str(uuid.uuid4())
    request.state.request_id = request_id
    response = await call_next(request)
    response.headers["X-Request-ID"] = request_id
    return response

# Request logging middleware
@app.middleware("http")
async def log_requests(request, call_next):
    start_time = datetime.now()
    response = await call_next(request)
    process_time = (datetime.now() - start_time).total_seconds() * 1000
    logger.info(
        f"Request {request.method} {request.url.path} "
        f"completed in {process_time:.2f}ms with status {response.status_code}"
    )
    return response

# Include routers
app.include_router(health_router, prefix="/api/v1/health", tags=["Health"])
app.include_router(metrics_router, prefix="/api/v1/metrics", tags=["Metrics"])
app.include_router(alerts_router, prefix="/api/v1/alerts", tags=["Alerts"])
app.include_router(trading_router, prefix="/api/v1/trading", tags=["Trading"])

# Root endpoint
@app.get("/", tags=["Root"])
async def root():
    return {
        "message": "Trading System Monitoring API",
        "version": "1.0.0",
        "docs_url": "/docs",
    }

# Error handlers
@app.exception_handler(HTTPException)
async def http_exception_handler(request, exc):
    return JSONResponse(
        status_code=exc.status_code,
        content={"error": exc.detail}
    )

@app.exception_handler(Exception)
async def general_exception_handler(request, exc):
    logger.error(f"Unhandled exception: {str(exc)}", exc_info=True)
    return JSONResponse(
        status_code=500,
        content={"error": "Internal server error"}
    )

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "app:app",
        host=settings.HOST,
        port=settings.PORT,
        reload=settings.DEBUG
    )