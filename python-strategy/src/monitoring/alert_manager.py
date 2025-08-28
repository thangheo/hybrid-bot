
# src/monitoring/alert_manager.py
import asyncio
import requests
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from datetime import datetime, timedelta
from typing import Dict, List, Optional
import uuid
import logging

logger = logging.getLogger(__name__)

class TelegramAlertChannel:
    """Simple Telegram alerts using requests"""
    
    def __init__(self, config: dict):
        self.bot_token = config.get('bot_token')
        self.chat_id = config.get('chat_id')
        self.enabled = config.get('enabled', False)
        
        if self.enabled and not (self.bot_token and self.chat_id):
            raise ValueError("Telegram bot_token and chat_id required")
    
    async def send_alert(self, alert: Dict) -> bool:
        """Send alert via Telegram"""
        if not self.enabled:
            return False
            
        try:
            message = self._format_message(alert)
            url = f"https://api.telegram.org/bot{self.bot_token}/sendMessage"
            
            payload = {
                'chat_id': self.chat_id,
                'text': message,
                'parse_mode': 'HTML'
            }
            
            response = requests.post(url, json=payload, timeout=10)
            response.raise_for_status()
            
            logger.info(f"Telegram alert sent: {alert['level']}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to send Telegram alert: {e}")
            return False
    
    def _format_message(self, alert: Dict) -> str:
        """Format alert for Telegram"""
        level_emoji = {
            'INFO': '🔵',
            'WARNING': '🟡', 
            'ERROR': '🔴',
            'CRITICAL': '🚨'
        }
        
        emoji = level_emoji.get(alert['level'], '⚪')
        timestamp = alert['timestamp'].strftime('%Y-%m-%d %H:%M:%S')
        
        message = f"""
{emoji} <b>{alert['level']} Alert</b>

<b>Message:</b> {alert['message']}
<b>Time:</b> {timestamp}
"""
        
        if alert.get('details'):
            details = alert['details']
            if isinstance(details, dict):
                for key, value in details.items():
                    message += f"\n<b>{key.title()}:</b> {value}"
        
        return message.strip()


class SlackAlertChannel:
    """Simple Slack alerts using webhook"""
    
    def __init__(self, config: dict):
        self.webhook_url = config.get('webhook_url')
        self.channel = config.get('channel', '#trading-alerts')
        self.enabled = config.get('enabled', False)
        
        if self.enabled and not self.webhook_url:
            raise ValueError("Slack webhook_url required")
    
    async def send_alert(self, alert: Dict) -> bool:
        """Send alert via Slack"""
        if not self.enabled:
            return False
            
        try:
            message = self._format_message(alert)
            
            payload = {
                'channel': self.channel,
                'text': message,
                'username': 'Trading Bot'
            }
            
            response = requests.post(self.webhook_url, json=payload, timeout=10)
            response.raise_for_status()
            
            logger.info(f"Slack alert sent: {alert['level']}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to send Slack alert: {e}")
            return False
    
    def _format_message(self, alert: Dict) -> str:
        """Format alert for Slack"""
        level_emoji = {
            'INFO': ':large_blue_circle:',
            'WARNING': ':warning:',
            'ERROR': ':red_circle:',
            'CRITICAL': ':rotating_light:'
        }
        
        emoji = level_emoji.get(alert['level'], ':white_circle:')
        timestamp = alert['timestamp'].strftime('%Y-%m-%d %H:%M:%S')
        
        message = f"{emoji} *{alert['level']} Alert*\n"
        message += f"*Message:* {alert['message']}\n"
        message += f"*Time:* {timestamp}"
        
        if alert.get('details'):
            message += "\n*Details:*"
            details = alert['details']
            if isinstance(details, dict):
                for key, value in details.items():
                    message += f"\n• {key}: {value}"
        
        return message


class EmailAlertChannel:
    """Simple email alerts using smtplib"""
    
    def __init__(self, config: dict):
        self.smtp_server = config.get('smtp_server', 'smtp.gmail.com')
        self.smtp_port = config.get('smtp_port', 587)
        self.username = config.get('username')
        self.password = config.get('password')
        self.recipients = config.get('recipients', [])
        self.enabled = config.get('enabled', False)
        
        if self.enabled and not (self.username and self.password):
            raise ValueError("Email username and password required")
    
    async def send_alert(self, alert: Dict) -> bool:
        """Send alert via email"""
        if not self.enabled or not self.recipients:
            return False
            
        try:
            # Run in thread since smtplib is blocking
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self._send_email, alert)
            
            logger.info(f"Email alert sent: {alert['level']}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to send email alert: {e}")
            return False
    
    def _send_email(self, alert: Dict):
        """Send email (blocking)"""
        msg = MIMEMultipart()
        msg['From'] = self.username
        msg['To'] = ', '.join(self.recipients)
        msg['Subject'] = f"Trading Bot {alert['level']} Alert"
        
        body = self._format_message(alert)
        msg.attach(MIMEText(body, 'plain'))
        
        with smtplib.SMTP(self.smtp_server, self.smtp_port) as server:
            server.starttls()
            server.login(self.username, self.password)
            server.send_message(msg)
    
    def _format_message(self, alert: Dict) -> str:
        """Format alert for email"""
        timestamp = alert['timestamp'].strftime('%Y-%m-%d %H:%M:%S')
        
        message = f"Trading Bot Alert\n"
        message += f"Level: {alert['level']}\n"
        message += f"Message: {alert['message']}\n"
        message += f"Time: {timestamp}\n"
        
        if alert.get('details'):
            message += "\nDetails:\n"
            details = alert['details']
            if isinstance(details, dict):
                for key, value in details.items():
                    message += f"  {key}: {value}\n"
        
        return message


class AlertManager:
    """Manage system alerts with rate limiting"""
    
    def __init__(self, config: dict):
        self.config = config
        self.channels = self._setup_channels(config)
        self.alert_history: List[Dict] = []
        
        # Rate limiting (seconds between same level alerts)
        self.rate_limits = config.get('rate_limits', {
            'ERROR': 300,    # 5 minutes
            'WARNING': 600,  # 10 minutes
            'INFO': 60       # 1 minute
        })
    
    def _setup_channels(self, config: dict) -> List:
        """Setup alert channels"""
        channels = []
        
        # Telegram
        if config.get('telegram', {}).get('enabled', False):
            channels.append(TelegramAlertChannel(config['telegram']))
        
        # Slack
        if config.get('slack', {}).get('enabled', False):
            channels.append(SlackAlertChannel(config['slack']))
        
        # Email
        if config.get('email', {}).get('enabled', False):
            channels.append(EmailAlertChannel(config['email']))
        
        return channels
    
    async def send_alert(self, level: str, message: str, details: Optional[Dict] = None):
        """Send alert through all configured channels"""
        # Check rate limiting
        if self._is_rate_limited(level):
            logger.debug(f"Alert rate limited: {level}")
            return
        
        alert = {
            'id': str(uuid.uuid4()),
            'timestamp': datetime.now(),
            'level': level.upper(),
            'message': message,
            'details': details or {}
        }
        
        # Store alert
        self.alert_history.append(alert)
        
        # Keep only recent alerts (last 24 hours)
        cutoff_time = datetime.now() - timedelta(hours=24)
        self.alert_history = [a for a in self.alert_history if a['timestamp'] > cutoff_time]
        
        # Send through all channels
        for channel in self.channels:
            try:
                await channel.send_alert(alert)
            except Exception as e:
                logger.error(f"Failed to send alert through {type(channel).__name__}: {e}")
        
        # Also log locally
        log_level = getattr(logging, level.upper(), logging.INFO)
        logger.log(log_level, f"ALERT: {message}")
    
    def _is_rate_limited(self, level: str) -> bool:
        """Check if alert level is rate limited"""
        rate_limit = self.rate_limits.get(level.upper(), 60)
        cutoff_time = datetime.now() - timedelta(seconds=rate_limit)
        
        recent_alerts = [
            alert for alert in self.alert_history
            if alert['timestamp'] > cutoff_time and alert['level'] == level.upper()
        ]
        
        return len(recent_alerts) > 0

