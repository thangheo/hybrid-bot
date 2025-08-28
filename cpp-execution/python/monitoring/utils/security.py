# monitoring/utils/security.py

import os
import base64
import hashlib
import hmac
import logging
import jwt
from datetime import datetime, timedelta
from ..config.settings import settings

logger = logging.getLogger(__name__)

def generate_secure_token(length=32):
    """Generate a secure random token."""
    return base64.urlsafe_b64encode(os.urandom(length)).decode('utf-8')

def hash_password(password, salt=None):
    """
    Hash a password using PBKDF2.
    
    Args:
        password: Plain text password
        salt: Optional salt, generated if not provided
        
    Returns:
        Tuple of (hashed_password, salt)
    """
    if salt is None:
        salt = os.urandom(32)
    elif isinstance(salt, str):
        salt = salt.encode('utf-8')
        
    hashed = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt,
        100000
    )
    
    return base64.b64encode(hashed).decode('utf-8'), base64.b64encode(salt).decode('utf-8')

def verify_password(password, hashed_password, salt):
    """
    Verify a password against its hash.
    
    Args:
        password: Plain text password to verify
        hashed_password: Base64-encoded hash of the password
        salt: Base64-encoded salt used to hash the password
        
    Returns:
        True if the password matches, False otherwise
    """
    if isinstance(salt, str):
        salt = base64.b64decode(salt)
        
    calculated_hash, _ = hash_password(password, salt)
    return calculated_hash == hashed_password

def generate_hmac(message, secret):
    """
    Generate an HMAC for message authentication.
    
    Args:
        message: Message to authenticate
        secret: Secret key
        
    Returns:
        Base64-encoded HMAC
    """
    if isinstance(message, str):
        message = message.encode('utf-8')
    if isinstance(secret, str):
        secret = secret.encode('utf-8')
        
    signature = hmac.new(secret, message, hashlib.sha256)
    return base64.b64encode(signature.digest()).decode('utf-8')

def verify_hmac(message, signature, secret):
    """
    Verify an HMAC signature.
    
    Args:
        message: Original message
        signature: Base64-encoded HMAC signature
        secret: Secret key
        
    Returns:
        True if signature is valid, False otherwise
    """
    calculated_signature = generate_hmac(message, secret)
    return hmac.compare_digest(calculated_signature, signature)

def generate_jwt(payload, expiration=None):
    """
    Generate a JWT token.
    
    Args:
        payload: Dictionary of claims
        expiration: Optional expiration time in seconds
        
    Returns:
        JWT token string
    """
    secret = settings.get("security.jwt_secret", "DEFAULT_SECRET_CHANGE_ME")
    
    if expiration:
        exp_time = datetime.utcnow() + timedelta(seconds=expiration)
        payload["exp"] = exp_time
        
    try:
        return jwt.encode(payload, secret, algorithm="HS256")
    except Exception as e:
        logger.error(f"Error generating JWT: {e}")
        return None

def verify_jwt(token):
    """
    Verify and decode a JWT token.
    
    Args:
        token: JWT token string
        
    Returns:
        Decoded claims if valid, None otherwise
    """
    secret = settings.get("security.jwt_secret", "DEFAULT_SECRET_CHANGE_ME")
    
    try:
        return jwt.decode(token, secret, algorithms=["HS256"])
    except jwt.ExpiredSignatureError:
        logger.warning("JWT token expired")
        return None
    except jwt.InvalidTokenError as e:
        logger.warning(f"Invalid JWT token: {e}")
        return None