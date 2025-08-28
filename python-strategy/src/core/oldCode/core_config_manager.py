# src/core/config_manager.py
"""
Configuration Management System
Handles loading, validation, and hot-reloading of configuration files
"""

import os
import re
import yaml
import json
from pathlib import Path
from typing import Dict, Any, List, Optional
from datetime import datetime
import logging
from dataclasses import dataclass

logger = logging.getLogger(__name__)


@dataclass
class ConfigValidationError(Exception):
    """Raised when configuration validation fails"""
    section: str
    message: str
    
    def __str__(self):
        return f"Config validation error in section '{self.section}': {self.message}"


class ConfigurationManager:
    """
    Manages system configuration with hot reloading and validation.
    Supports environment variable expansion and multi-environment configs.
    """
    
    def __init__(self, config_path: str, environment: str = "development"):
        self.config_path = Path(config_path)
        self.environment = environment
        self.config: Dict[str, Any] = {}
        self.config_watchers: List = []
        self.last_modified: Optional[float] = None
        
        # Required configuration sections
        self.required_sections = [
            'data_ingestion',
            'strategy', 
            'risk_management',
            'order_execution',
            'monitoring'
        ]
        
    def load_config(self) -> Dict[str, Any]:
        """Load configuration from file with validation"""
        if not self.config_path.exists():
            raise FileNotFoundError(f"Config file not found: {self.config_path}")
            
        # Check if file was modified
        current_modified = self.config_path.stat().st_mtime
        if self.last_modified and current_modified == self.last_modified:
            return self.config
            
        try:
            with open(self.config_path, 'r') as f:
                if self.config_path.suffix.lower() == '.yaml':
                    new_config = yaml.safe_load(f)
                elif self.config_path.suffix.lower() == '.json':
                    new_config = json.load(f)
                else:
                    raise ValueError(f"Unsupported config file format: {self.config_path.suffix}")
            
            # Load environment-specific overrides
            new_config = self._load_environment_overrides(new_config)
            
            # Expand environment variables
            new_config = self._expand_env_vars(new_config)
            
            # Validate configuration
            self._validate_config(new_config)
            
            self.config = new_config
            self.last_modified = current_modified
            
            logger.info(f"Configuration loaded successfully from {self.config_path}")
            return self.config
            
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            raise
    
    def _load_environment_overrides(self, base_config: Dict[str, Any]) -> Dict[str, Any]:
        """Load environment-specific configuration overrides"""
        env_config_path = self.config_path.parent / "environments" / f"{self.environment}.yaml"
        
        if not env_config_path.exists():
            logger.debug(f"No environment override found for {self.environment}")
            return base_config
            
        try:
            with open(env_config_path, 'r') as f:
                env_config = yaml.safe_load(f)
                
            # Deep merge environment config into base config
            merged_config = self._deep_merge(base_config, env_config)
            logger.info(f"Applied environment overrides for {self.environment}")
            return merged_config
            
        except Exception as e:
            logger.warning(f"Failed to load environment overrides: {e}")
            return base_config
    
    def _deep_merge(self, base: Dict, override: Dict) -> Dict:
        """Deep merge two dictionaries"""
        result = base.copy()
        
        for key, value in override.items():
            if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                result[key] = self._deep_merge(result[key], value)
            else:
                result[key] = value
                
        return result
    
    def _expand_env_vars(self, config: Dict[str, Any]) -> Dict[str, Any]:
        """Expand environment variables in configuration values"""
        def expand_value(value):
            if isinstance(value, str):
                # Replace ${VAR} and $VAR patterns with environment variables
                pattern = re.compile(r'\$\{([^}]+)\}|\$([A-Za-z_][A-Za-z0-9_]*)')
                
                def replacer(match):
                    var_name = match.group(1) or match.group(2)
                    env_value = os.getenv(var_name)
                    if env_value is None:
                        logger.warning(f"Environment variable {var_name} not found, keeping original")
                        return match.group(0)
                    return env_value
                    
                return pattern.sub(replacer, value)
                
            elif isinstance(value, dict):
                return {k: expand_value(v) for k, v in value.items()}
                
            elif isinstance(value, list):
                return [expand_value(item) for item in value]
                
            else:
                return value
                
        return expand_value(config)
    
    def _validate_config(self, config: Dict[str, Any]) -> None:
        """Validate configuration structure and values"""
        # Check required sections
        for section in self.required_sections:
            if section not in config:
                raise ConfigValidationError(section, f"Missing required section: {section}")
        
        # Validate data ingestion section
        self._validate_data_ingestion(config.get('data_ingestion', {}))
        
        # Validate strategy section
        self._validate_strategy(config.get('strategy', {}))
        
        # Validate risk management section
        self._validate_risk_management(config.get('risk_management', {}))
        
        # Validate order execution section
        self._validate_order_execution(config.get('order_execution', {}))
        
        # Validate monitoring section
        self._validate_monitoring(config.get('monitoring', {}))
    
    def _validate_data_ingestion(self, config: Dict[str, Any]) -> None:
        """Validate data ingestion configuration"""
        section = 'data_ingestion'
        
        if not config.get('binance', {}).get('api_key'):
            raise ConfigValidationError(section, "Binance API key is required")
            
        if not config.get('binance', {}).get('api_secret'):
            raise ConfigValidationError(section, "Binance API secret is required")
            
        symbols = config.get('symbols', [])
        if not symbols or not isinstance(symbols, list):
            raise ConfigValidationError(section, "At least one trading symbol must be specified")
            
        timeframes = config.get('timeframes', [])
        if not timeframes or not isinstance(timeframes, list):
            raise ConfigValidationError(section, "At least one timeframe must be specified")
    
    def _validate_strategy(self, config: Dict[str, Any]) -> None:
        """Validate strategy configuration"""
        section = 'strategy'
        
        active_strategies = config.get('active_strategies', [])
        if not active_strategies:
            raise ConfigValidationError(section, "At least one active strategy must be configured")
            
        total_weight = sum(strategy.get('weight', 0) for strategy in active_strategies)
        if abs(total_weight - 1.0) > 0.01:  # Allow small floating point errors
            raise ConfigValidationError(section, f"Strategy weights must sum to 1.0, got {total_weight}")
    
    def _validate_risk_management(self, config: Dict[str, Any]) -> None:
        """Validate risk management configuration"""
        section = 'risk_management'
        
        max_portfolio_risk = config.get('max_portfolio_risk', 0)
        if max_portfolio_risk <= 0:
            raise ConfigValidationError(section, "max_portfolio_risk must be positive")
            
        max_position_risk = config.get('max_position_risk', 0)  
        if max_position_risk <= 0:
            raise ConfigValidationError(section, "max_position_risk must be positive")
            
        if max_position_risk > max_portfolio_risk:
            raise ConfigValidationError(section, "max_position_risk cannot exceed max_portfolio_risk")
    
    def _validate_order_execution(self, config: Dict[str, Any]) -> None:
        """Validate order execution configuration"""
        section = 'order_execution'
        
        if not config.get('binance', {}).get('api_key'):
            raise ConfigValidationError(section, "Binance API key is required for order execution")
            
        max_slippage = config.get('execution', {}).get('max_slippage', 0)
        if max_slippage <= 0 or max_slippage > 0.1:  # Max 10% slippage
            raise ConfigValidationError(section, "max_slippage must be between 0 and 0.1")
    
    def _validate_monitoring(self, config: Dict[str, Any]) -> None:
        """Validate monitoring configuration"""
        section = 'monitoring'
        
        memory_threshold = config.get('memory_threshold', 0)
        if not (0 < memory_threshold < 1):
            raise ConfigValidationError(section, "memory_threshold must be between 0 and 1")
            
        cpu_threshold = config.get('cpu_threshold', 0)
        if not (0 < cpu_threshold < 1):
            raise ConfigValidationError(section, "cpu_threshold must be between 0 and 1")
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value with dot notation support"""
        keys = key.split('.')
        value = self.config
        
        try:
            for k in keys:
                value = value[k]
            return value
        except (KeyError, TypeError):
            return default
    
    def set(self, key: str, value: Any) -> None:
        """Set configuration value with dot notation support"""
        keys = key.split('.')
        config = self.config
        
        for k in keys[:-1]:
            if k not in config:
                config[k] = {}
            config = config[k]
            
        config[keys[-1]] = value
    
    def reload_if_changed(self) -> bool:
        """Check if config file changed and reload if needed"""
        if not self.config_path.exists():
            return False
            
        current_modified = self.config_path.stat().st_mtime
        if self.last_modified and current_modified != self.last_modified:
            try:
                self.load_config()
                logger.info("Configuration reloaded due to file change")
                return True
            except Exception as e:
                logger.error(f"Failed to reload configuration: {e}")
                return False
        return False
    
    def save_config(self, path: Optional[Path] = None) -> None:
        """Save current configuration to file"""
        save_path = path or self.config_path
        
        try:
            with open(save_path, 'w') as f:
                if save_path.suffix.lower() == '.yaml':
                    yaml.dump(self.config, f, default_flow_style=False, indent=2)
                elif save_path.suffix.lower() == '.json':
                    json.dump(self.config, f, indent=2)
                else:
                    raise ValueError(f"Unsupported config file format: {save_path.suffix}")
                    
            logger.info(f"Configuration saved to {save_path}")
            
        except Exception as e:
            logger.error(f"Failed to save configuration: {e}")
            raise
    
    def get_section(self, section_name: str) -> Dict[str, Any]:
        """Get entire configuration section"""
        return self.config.get(section_name, {})
    
    def validate_section(self, section_name: str, schema: Dict[str, Any]) -> bool:
        """Validate a configuration section against a schema"""
        # Simple schema validation - could be extended with jsonschema
        section = self.get_section(section_name)
        
        for key, expected_type in schema.items():
            if key not in section:
                logger.warning(f"Missing key '{key}' in section '{section_name}'")
                return False
                
            if not isinstance(section[key], expected_type):
                logger.warning(f"Invalid type for '{key}' in section '{section_name}': expected {expected_type}, got {type(section[key])}")
                return False
                
        return True
    
    def get_environment(self) -> str:
        """Get current environment"""
        return self.environment
    
    def is_production(self) -> bool:
        """Check if running in production environment"""
        return self.environment.lower() == 'production'
    
    def is_development(self) -> bool:
        """Check if running in development environment"""
        return self.environment.lower() == 'development'
    
    def get_log_level(self) -> str:
        """Get configured log level"""
        return self.get('logging.level', 'INFO')
    
    def get_database_url(self) -> str:
        """Get database connection URL"""
        return self.get('data_ingestion.database.url', '')
    
    def get_binance_credentials(self) -> Dict[str, str]:
        """Get Binance API credentials"""
        return {
            'api_key': self.get('data_ingestion.binance.api_key', ''),
            'api_secret': self.get('data_ingestion.binance.api_secret', ''),
            'testnet': self.get('data_ingestion.binance.testnet', True)
        }
    
    def get_trading_symbols(self) -> List[str]:
        """Get list of trading symbols"""
        return self.get('data_ingestion.symbols', [])
    
    def get_active_strategies(self) -> List[Dict[str, Any]]:
        """Get active trading strategies"""
        return self.get('strategy.active_strategies', [])
    
    def __str__(self) -> str:
        return f"ConfigurationManager(path={self.config_path}, env={self.environment})"