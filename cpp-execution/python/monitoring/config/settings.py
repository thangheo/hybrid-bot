# monitoring/config/settings.py

import os
import yaml
import logging
from pathlib import Path

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
)
logger = logging.getLogger(__name__)

# Base Paths
BASE_DIR = Path(__file__).resolve().parent.parent.parent
CONFIG_DIR = BASE_DIR / "config"
DEFAULT_CONFIG_PATH = CONFIG_DIR / "monitoring" / "default_settings.yaml"

class Settings:
    """
    Central configuration manager for monitoring system.
    Loads settings from YAML files and environment variables.
    """
    def __init__(self, config_path=None):
        self.config_path = config_path or DEFAULT_CONFIG_PATH
        self._config = {}
        self.load_config()
        
    def load_config(self):
        """Load configuration from YAML file and override with environment variables."""
        try:
            with open(self.config_path, 'r') as f:
                self._config = yaml.safe_load(f) or {}
            logger.info(f"Loaded configuration from {self.config_path}")
            
            # Override with environment variables
            self._override_from_env()
        except FileNotFoundError:
            logger.warning(f"Config file not found at {self.config_path}, using defaults")
            self._config = {}
        except Exception as e:
            logger.error(f"Error loading configuration: {e}")
            raise
            
    def _override_from_env(self):
        """Override configuration settings with environment variables."""
        # Look for environment variables with prefix MONITOR_
        for env_name, env_value in os.environ.items():
            if env_name.startswith("MONITOR_"):
                # Convert MONITOR_DATABASE_HOST to database.host
                config_name = env_name[8:].lower().replace("_", ".")
                self._set_nested_value(self._config, config_name, env_value)
                logger.debug(f"Override {config_name} from environment variable")
                
    def _set_nested_value(self, config_dict, path, value):
        """Set a value in a nested dictionary using dot notation path."""
        parts = path.split(".")
        for part in parts[:-1]:
            if part not in config_dict:
                config_dict[part] = {}
            config_dict = config_dict[part]
        config_dict[parts[-1]] = value
                
    def get(self, path, default=None):
        """Get a configuration value using dot notation path."""
        try:
            parts = path.split(".")
            value = self._config
            for part in parts:
                value = value[part]
            return value
        except (KeyError, TypeError):
            return default
            
    def set(self, path, value):
        """Set a configuration value using dot notation path."""
        self._set_nested_value(self._config, path, value)

# Create a singleton instance
settings = Settings()