# monitoring/utils/notification.py

import logging
import smtplib
import requests
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from ..config.settings import settings
from ..config.alerts import alerts_config

logger = logging.getLogger(__name__)

class NotificationManager:
    """Manages sending notifications through various channels."""
    
    def __init__(self):
        self.channels = alerts_config.notification_channels
        
    def send_notification(self, title, message, severity="info", metadata=None):
        """
        Send a notification through all enabled channels.
        
        Args:
            title: Notification title
            message: Notification message
            severity: Alert severity (info, warning, critical)
            metadata: Additional metadata for the notification
            
        Returns:
            Dictionary with results for each channel
        """
        results = {}
        metadata = metadata or {}
        
        for channel, config in self.channels.items():
            if not config.get("enabled", False):
                continue
                
            try:
                if channel == "email":
                    result = self._send_email(title, message, severity, config, metadata)
                elif channel == "slack":
                    result = self._send_slack(title, message, severity, config, metadata)
                else:
                    logger.warning(f"Unknown notification channel: {channel}")
                    result = False
                    
                results[channel] = result
            except Exception as e:
                logger.error(f"Error sending {channel} notification: {e}")
                results[channel] = False
                
        return results
        
    def _send_email(self, title, message, severity, config, metadata):
        """Send an email notification."""
        recipients = config.get("recipients", [])
        if not recipients:
            logger.warning("No email recipients configured")
            return False
            
        smtp_host = settings.get("notifications.email.smtp_host", "localhost")
        smtp_port = settings.get("notifications.email.smtp_port", 25)
        smtp_user = settings.get("notifications.email.smtp_user", "")
        smtp_password = settings.get("notifications.email.smtp_password", "")
        from_address = settings.get("notifications.email.from", "monitoring@example.com")
        
        try:
            msg = MIMEMultipart()
            msg["From"] = from_address
            msg["To"] = ", ".join(recipients)
            msg["Subject"] = f"[{severity.upper()}] {title}"
            
            body = message
            if metadata:
                body += "\n\nAdditional Information:\n"
                for key, value in metadata.items():
                    body += f"{key}: {value}\n"
                    
            msg.attach(MIMEText(body, "plain"))
            
            with smtplib.SMTP(smtp_host, smtp_port) as server:
                if smtp_user and smtp_password:
                    server.starttls()
                    server.login(smtp_user, smtp_password)
                    
                server.send_message(msg)
                
            logger.info(f"Sent email notification to {len(recipients)} recipients")
            return True
        except Exception as e:
            logger.error(f"Failed to send email: {e}")
            return False
            
    def _send_slack(self, title, message, severity, config, metadata):
        """Send a Slack notification."""
        webhook_url = config.get("webhook_url")
        if not webhook_url:
            logger.warning("No Slack webhook URL configured")
            return False
            
        channel = config.get("channel")
        
        # Determine color based on severity
        severity_colors = {
            "info": "#36a64f",
            "warning": "#ffcc00",
            "critical": "#ff0000"
        }
        color = severity_colors.get(severity, "#36a64f")
        
        # Create payload
        payload = {
            "text": f"*{severity.upper()}*: {title}",
            "attachments": [
                {
                    "color": color,
                    "text": message,
                    "fields": []
                }
            ]
        }
        
        if channel:
            payload["channel"] = channel
            
        # Add metadata as fields
        if metadata:
            for key, value in metadata.items():
                payload["attachments"][0]["fields"].append({
                    "title": key,
                    "value": str(value),
                    "short": True
                })
                
        try:
            response = requests.post(webhook_url, json=payload)
            response.raise_for_status()
            logger.info(f"Sent Slack notification to {channel or 'default channel'}")
            return True
        except Exception as e:
            logger.error(f"Failed to send Slack notification: {e}")
            return False

# Create singleton instance
notification_manager = NotificationManager()