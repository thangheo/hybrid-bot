# src/core/config.py
"""
Bio-Inspired Trading System - Configuration Management
Centralized configuration with hot-reload and validation capabilities
"""

import os
import yaml
import json
import logging
from typing import Dict, Any, Optional, List, Callable, Union
from pathlib import Path
import asyncio
from datetime import datetime
from dataclasses import dataclass, field
import re
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
import threading


logger = logging.getLogger(__name__)


@dataclass
class ConfigSection:
    """Configuration section with validation and metadata"""
    name: str
    data: Dict[str, Any]
    schema: Optional[Dict[str, Any]] = None
    last_updated: datetime = field(default_factory=datetime.now)
    is_valid: bool = True
    validation_errors: List[str] = field(default_factory=list)


class ConfigValidator:
    """Configuration validation with bio-inspired validation rules"""
    
    @staticmethod
    def validate_layer_config(layer_name: str, config: Dict[str, Any]) -> List[str]:
        """Validate layer-specific configuration"""
        errors = []
        
        # Common validation rules for all layers
        if 'enabled' in config and not isinstance(config['enabled'], bool):
            errors.append(f"{layer_name}.enabled must be boolean")
        
        # Layer-specific validation
        layer_validators = {
            'consciousness': ConfigValidator._validate_consciousness_config,
            'adaptation': ConfigValidator._validate_adaptation_config,
            'homeostasis': ConfigValidator._validate_homeostasis_config,
            'perception': ConfigValidator._validate_perception_config,
            'execution': ConfigValidator._validate_execution_config
        }
        
        if layer_name in layer_validators:
            layer_errors = layer_validators[layer_name](config)
            errors.extend(layer_errors)
        
        return errors
    
    @staticmethod
    def _validate_consciousness_config(config: Dict[str, Any]) -> List[str]:
        """Validate consciousness layer configuration"""
        errors = []
        
        if 'health_monitoring' in config:
            health_config = config['health_monitoring']
            if 'stability_threshold' in health_config:
                threshold = health_config['stability_threshold']
                if not isinstance(threshold, (int, float)) or not (0 <= threshold <= 100):
                    errors.append("consciousness.health_monitoring.stability_threshold must be 0-100")
        
        if 'regime_detection' in config:
            regime_config = config['regime_detection']
            if 'confidence_threshold' in regime_config:
                confidence = regime_config['confidence_threshold']
                if not isinstance(confidence, (int, float)) or not (0 <= confidence <= 1):
                    errors.append("consciousness.regime_detection.confidence_threshold must be 0-1")
        
        return errors
    
    @staticmethod
    def _validate_adaptation_config(config: Dict[str, Any]) -> List[str]:
        """Validate adaptation layer configuration"""
        errors = []
        
        if 'learning_rate' in config:
            lr = config['learning_rate']
            if not isinstance(lr, (int, float)) or not (0 < lr <= 1):
                errors.append("adaptation.learning_rate must be > 0 and <= 1")
        
        if 'parameter_bounds' in config:
            bounds = config['parameter_bounds']
            for param, bound in bounds.items():
                if not isinstance(bound, list) or len(bound) != 2:
                    errors.append(f"adaptation.parameter_bounds.{param} must be [min, max] list")
                elif bound[0] >= bound[1]:
                    errors.append(f"adaptation.parameter_bounds.{param} min must be < max")
        
        return errors
    
    @staticmethod
    def _validate_homeostasis_config(config: Dict[str, Any]) -> List[str]:
        """Validate homeostasis layer configuration"""
        errors = []
        
        risk_params = ['max_portfolio_risk', 'max_position_risk', 'max_drawdown_limit']
        for param in risk_params:
            if param in config:
                value = config[param]
                if not isinstance(value, (int, float)) or not (0 < value <= 1):
                    errors.append(f"homeostasis.{param} must be > 0 and <= 1")
        
        if 'stability_target' in config:
            target = config['stability_target']
            if not isinstance(target, (int, float)) or not (0 <= target <= 100):
                errors.append("homeostasis.stability_target must be 0-100")
        
        return errors
    
    @staticmethod
    def _validate_perception_config(config: Dict[str, Any]) -> List[str]:
        """Validate perception layer configuration"""
        errors = []
        
        if 'market_regimes' in config:
            regimes = config['market_regimes']
            if not isinstance(regimes, list):
                errors.append("perception.market_regimes must be a list")
            else:
                valid_regimes = ['trend', 'mean_reversion', 'high_volatility', 'low_volatility', 'sideways', 'crisis']
                for regime in regimes:
                    if regime not in valid_regimes:
                        errors.append(f"perception.market_regimes contains invalid regime: {regime}")
        
        if 'confidence_decay_rate' in config:
            decay = config['confidence_decay_rate']
            if not isinstance(decay, (int, float)) or not (0 < decay <= 1):
                errors.append("perception.confidence_decay_rate must be > 0 and <= 1")
        
        return errors
    
    @staticmethod
    def _validate_execution_config(config: Dict[str, Any]) -> List[str]:
        """Validate execution layer configuration"""
        errors = []
        
        if 'binance' in config:
            binance_config = config['binance']
            required_fields = ['api_key', 'api_secret']
            for field in required_fields:
                if field not in binance_config or not binance_config[field]:
                    errors.append(f"execution.binance.{field} is required")
        
        if 'max_slippage' in config:
            slippage = config['max_slippage']
            if not isinstance(slippage, (int, float)) or not (0 <= slippage <= 1):
                errors.append("execution.max_slippage must be 0-1")
        
        return errors


class ConfigWatcher(FileSystemEventHandler):
    """File system watcher for configuration changes"""
    
    def __init__(self, config_manager: 'ConfigManager'):
        self.config_manager = config_manager
        self.last_modified = {}
    
    def on_modified(self, event):
        """Handle file modification events"""
        if event.is_directory:
            return
        
        # Check if it's a config file
        if not event.src_path.endswith(('.yaml', '.yml', '.json')):
            return
        
        # Debounce rapid file changes
        current_time = datetime.now().timestamp()
        if event.src_path in self.last_modified:
            if current_time - self.last_modified[event.src_path] < 1.0:  # 1 second debounce
                return
        
        self.last_modified[event.src_path] = current_time
        
        logger.info(f"Configuration file changed: {event.src_path}")
        
        # Trigger async reload
        asyncio.create_task(self.config_manager.reload_config())


class ConfigManager:
    """
    Bio-inspired configuration manager with adaptive configuration loading,
    hot-reload capabilities, and intelligent validation.
    """
    
    def __init__(self, config_path: str = "config/bio_trading_system.yaml"):
        self.config_path = Path(config_path)
        self.config_sections: Dict[str, ConfigSection] = {}
        self.reload_callbacks: List[Callable[[str, Dict[str, Any]], None]] = []
        
        # File watching
        self.observer: Optional[Observer] = None
        self.watcher: Optional[ConfigWatcher] = None
        
        # Configuration cache and validation
        self.validator = ConfigValidator()
        self.environment_overrides: Dict[str, Any] = {}
        
        # Thread safety
        self._lock = threading.RLock()
        
        # Load environment overrides
        self._load_environment_overrides()
    
    async def initialize(self) -> bool:
        """Initialize configuration manager"""
        try:
            # Load initial configuration
            await self.load_config()
            
            # Start file watcher
            self._start_file_watcher()
            
            logger.info("Configuration manager initialized successfully")
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize configuration manager: {e}")
            return False
    
    async def shutdown(self) -> None:
        """Shutdown configuration manager"""
        if self.observer:
            self.observer.stop()
            self.observer.join()
            logger.info("Configuration manager shutdown complete")
    
    async def load_config(self) -> Dict[str, Any]:
        """Load configuration from file with validation"""
        try:
            if not self.config_path.exists():
                raise FileNotFoundError(f"Configuration file not found: {self.config_path}")
            
            with self._lock:
                # Read configuration file
                config_data = self._read_config_file()
                
                # Expand environment variables
                config_data = self._expand_environment_variables(config_data)
                
                # Apply environment overrides
                config_data = self._apply_environment_overrides(config_data)
                
                # Validate configuration
                validation_results = self._validate_configuration(config_data)
                
                # Update configuration sections
                self._update_config_sections(config_data, validation_results)
                
                logger.info("Configuration loaded and validated successfully")
                return config_data
                
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            raise
    
    async def reload_config(self) -> bool:
        """Reload configuration and notify callbacks"""
        try:
            old_config = self.get_full_config()
            new_config = await self.load_config()
            
            # Find changed sections
            changed_sections = self._find_changed_sections(old_config, new_config)
            
            # Notify callbacks about changes
            for section_name in changed_sections:
                section_config = new_config.get(section_name, {})
                await self._notify_reload_callbacks(section_name, section_config)
            
            if changed_sections:
                logger.info(f"Configuration reloaded. Changed sections: {changed_sections}")
            
            return True
            
        except Exception as e:
            logger.error(f"Failed to reload configuration: {e}")
            return False
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value using dot notation (e.g., 'layer.setting')"""
        with self._lock:
            try:
                keys = key.split('.')
                current = self.get_full_config()
                
                for k in keys:
                    if isinstance(current, dict) and k in current:
                        current = current[k]
                    else:
                        return default
                
                return current
                
            except Exception:
                return default
    
    def get_layer_config(self, layer_name: str) -> Dict[str, Any]:
        """Get layer-specific configuration"""
        section_name = f"{layer_name}_layer" if not layer_name.endswith('_layer') else layer_name
        
        with self._lock:
            if section_name in self.config_sections:
                section = self.config_sections[section_name]
                if section.is_valid:
                    return section.data.copy()
                else:
                    logger.warning(f"Layer config {layer_name} has validation errors: {section.validation_errors}")
                    return {}
            
            # Fallback to direct section access
            return self.get(layer_name, {})
    
    def get_full_config(self) -> Dict[str, Any]:
        """Get complete configuration"""
        with self._lock:
            full_config = {}
            for section_name, section in self.config_sections.items():
                if section.is_valid:
                    full_config[section_name] = section.data
            return full_config
    
    def set(self, key: str, value: Any) -> bool:
        """Set configuration value (runtime only, not persisted)"""
        with self._lock:
            try:
                keys = key.split('.')
                section_name = keys[0]
                
                if section_name not in self.config_sections:
                    self.config_sections[section_name] = ConfigSection(
                        name=section_name,
                        data={}
                    )
                
                # Navigate to parent and set value
                current = self.config_sections[section_name].data
                for k in keys[1:-1]:
                    if k not in current:
                        current[k] = {}
                    current = current[k]
                
                current[keys[-1]] = value
                self.config_sections[section_name].last_updated = datetime.now()
                
                return True
                
            except Exception as e:
                logger.error(f"Failed to set config value {key}: {e}")
                return False
    
    def add_reload_callback(self, callback: Callable[[str, Dict[str, Any]], None]) -> None:
        """Add callback for configuration changes"""
        self.reload_callbacks.append(callback)
    
    def remove_reload_callback(self, callback: Callable[[str, Dict[str, Any]], None]) -> None:
        """Remove configuration change callback"""
        if callback in self.reload_callbacks:
            self.reload_callbacks.remove(callback)
    
    def get_config_health(self) -> Dict[str, Any]:
        """Get configuration health status"""
        with self._lock:
            health_info = {
                "total_sections": len(self.config_sections),
                "valid_sections": sum(1 for s in self.config_sections.values() if s.is_valid),
                "invalid_sections": sum(1 for s in self.config_sections.values() if not s.is_valid),
                "last_reload": max(
                    (s.last_updated for s in self.config_sections.values()),
                    default=datetime.now()
                ).isoformat(),
                "validation_errors": {}
            }
            
            for name, section in self.config_sections.items():
                if not section.is_valid:
                    health_info["validation_errors"][name] = section.validation_errors
            
            return health_info
    
    def _read_config_file(self) -> Dict[str, Any]:
        """Read and parse configuration file"""
        try:
            with open(self.config_path, 'r', encoding='utf-8') as file:
                if self.config_path.suffix.lower() in ['.yaml', '.yml']:
                    return yaml.safe_load(file) or {}
                elif self.config_path.suffix.lower() == '.json':
                    return json.load(file)
                else:
                    raise ValueError(f"Unsupported config file format: {self.config_path.suffix}")
                    
        except yaml.YAMLError as e:
            raise ValueError(f"Invalid YAML syntax: {e}")
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON syntax: {e}")
    
    def _expand_environment_variables(self, config: Any) -> Any:
        """Recursively expand environment variables in configuration"""
        if isinstance(config, dict):
            return {k: self._expand_environment_variables(v) for k, v in config.items()}
        elif isinstance(config, list):
            return [self._expand_environment_variables(item) for item in config]
        elif isinstance(config, str):
            # Replace ${VAR} and $VAR patterns
            def replace_env_var(match):
                var_name = match.group(1) or match.group(2)
                return os.getenv(var_name, match.group(0))
            
            # Pattern for ${VAR} and $VAR
            pattern = r'\$\{([^}]+)\}|\$([A-Za-z_][A-Za-z0-9_]*)'
            return re.sub(pattern, replace_env_var, config)
        else:
            return config
    
    def _load_environment_overrides(self) -> None:
        """Load configuration overrides from environment variables"""
        # Look for environment variables with BIO_TRADING_ prefix
        for key, value in os.environ.items():
            if key.startswith('BIO_TRADING_'):
                # Convert BIO_TRADING_LAYER_SETTING to layer.setting
                config_key = key[12:].lower().replace('_', '.')
                
                # Try to parse as JSON, otherwise keep as string
                try:
                    parsed_value = json.loads(value)
                    self.environment_overrides[config_key] = parsed_value
                except json.JSONDecodeError:
                    # Try to parse as number or boolean
                    if value.lower() in ['true', 'false']:
                        self.environment_overrides[config_key] = value.lower() == 'true'
                    elif value.isdigit():
                        self.environment_overrides[config_key] = int(value)
                    elif '.' in value and value.replace('.', '').isdigit():
                        self.environment_overrides[config_key] = float(value)
                    else:
                        self.environment_overrides[config_key] = value
    
    def _apply_environment_overrides(self, config: Dict[str, Any]) -> Dict[str, Any]:
        """Apply environment variable overrides to configuration"""
        for key, value in self.environment_overrides.items():
            keys = key.split('.')
            current = config
            
            # Navigate to parent
            for k in keys[:-1]:
                if k not in current:
                    current[k] = {}
                current = current[k]
            
            # Set the value
            current[keys[-1]] = value
            logger.debug(f"Applied environment override: {key} = {value}")
        
        return config
    
    def _validate_configuration(self, config: Dict[str, Any]) -> Dict[str, List[str]]:
        """Validate entire configuration and return errors by section"""
        validation_results = {}
        
        # Validate each layer configuration
        layer_names = ['consciousness', 'adaptation', 'homeostasis', 'perception', 'execution']
        
        for layer_name in layer_names:
            if layer_name in config:
                errors = self.validator.validate_layer_config(layer_name, config[layer_name])
                if errors:
                    validation_results[layer_name] = errors
        
        # Validate system-wide settings
        system_errors = self._validate_system_config(config)
        if system_errors:
            validation_results['system'] = system_errors
        
        return validation_results
    
    def _validate_system_config(self, config: Dict[str, Any]) -> List[str]:
        """Validate system-wide configuration"""
        errors = []
        
        # Validate logging configuration
        if 'logging' in config:
            logging_config = config['logging']
            if 'level' in logging_config:
                valid_levels = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
                if logging_config['level'] not in valid_levels:
                    errors.append(f"logging.level must be one of {valid_levels}")
        
        # Validate database configuration
        if 'database' in config:
            db_config = config['database']
            if 'url' in db_config and not db_config['url']:
                errors.append("database.url cannot be empty")
        
        # Validate feedback loop intervals
        if 'feedback_loops' in config:
            feedback_config = config['feedback_loops']
            intervals = ['primary_interval', 'secondary_interval', 'tertiary_interval']
            for interval in intervals:
                if interval in feedback_config:
                    value = feedback_config[interval]
                    if not isinstance(value, (int, float)) or value <= 0:
                        errors.append(f"feedback_loops.{interval} must be positive number")
        
        return errors
    
    def _update_config_sections(self, config: Dict[str, Any], 
                               validation_results: Dict[str, List[str]]) -> None:
        """Update configuration sections with validation results"""
        for section_name, section_data in config.items():
            errors = validation_results.get(section_name, [])
            
            self.config_sections[section_name] = ConfigSection(
                name=section_name,
                data=section_data,
                last_updated=datetime.now(),
                is_valid=len(errors) == 0,
                validation_errors=errors
            )
            
            if errors:
                logger.warning(f"Configuration section '{section_name}' has validation errors: {errors}")
    
    def _find_changed_sections(self, old_config: Dict[str, Any], 
                              new_config: Dict[str, Any]) -> List[str]:
        """Find configuration sections that have changed"""
        changed_sections = []
        
        # Check all sections in new config
        for section_name in new_config:
            if (section_name not in old_config or 
                old_config[section_name] != new_config[section_name]):
                changed_sections.append(section_name)
        
        # Check for removed sections
        for section_name in old_config:
            if section_name not in new_config:
                changed_sections.append(section_name)
        
        return changed_sections
    
    async def _notify_reload_callbacks(self, section_name: str, config: Dict[str, Any]) -> None:
        """Notify callbacks about configuration changes"""
        for callback in self.reload_callbacks:
            try:
                if asyncio.iscoroutinefunction(callback):
                    await callback(section_name, config)
                else:
                    callback(section_name, config)
            except Exception as e:
                logger.error(f"Error in config reload callback: {e}")
    
    def _start_file_watcher(self) -> None:
        """Start watching configuration file for changes"""
        try:
            if not self.config_path.parent.exists():
                return
            
            self.watcher = ConfigWatcher(self)
            self.observer = Observer()
            self.observer.schedule(
                self.watcher,
                str(self.config_path.parent),
                recursive=False
            )
            self.observer.start()
            
            logger.info(f"Started watching configuration file: {self.config_path}")
            
        except Exception as e:
            logger.warning(f"Failed to start config file watcher: {e}")


class ConfigurationFactory:
    """Factory for creating configuration managers with common setups"""
    
    @staticmethod
    def create_default_config_manager(config_path: Optional[str] = None) -> ConfigManager:
        """Create configuration manager with default settings"""
        if config_path is None:
            config_path = os.getenv('BIO_CONFIG_PATH', 'config/bio_trading_system.yaml')
        
        return ConfigManager(config_path)
    
    @staticmethod
    def create_test_config_manager(test_config: Dict[str, Any]) -> ConfigManager:
        """Create configuration manager for testing with in-memory config"""
        import tempfile
        import yaml
        
        # Create temporary config file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            yaml.dump(test_config, f)
            temp_path = f.name
        
        config_manager = ConfigManager(temp_path)
        
        # Clean up temp file on shutdown
        original_shutdown = config_manager.shutdown
        async def cleanup_shutdown():
            await original_shutdown()
            os.unlink(temp_path)
        config_manager.shutdown = cleanup_shutdown
        
        return config_manager


# Example configuration structure for bio-inspired trading system
DEFAULT_CONFIG_TEMPLATE = {
    'system': {
        'name': 'bio-inspired-trading-system',
        'version': '1.0.0',
        'environment': 'development',
        'debug': True
    },
    'consciousness': {
        'enabled': True,
        'health_monitoring': {
            'enabled': True,
            'check_interval': 60,
            'stability_threshold': 70
        },
        'regime_detection': {
            'lookback_period': 100,
            'confidence_threshold': 0.7
        }
    },
    'adaptation': {
        'enabled': True,
        'learning_rate': 0.01,
        'parameter_bounds': {
            'position_size': [0.001, 0.1],
            'stop_loss': [0.01, 0.1]
        }
    },
    'homeostasis': {
        'enabled': True,
        'stability_target': 75,
        'max_portfolio_risk': 0.02,
        'max_position_risk': 0.01,
        'max_drawdown_limit': 0.15
    },
    'perception': {
        'enabled': True,
        'market_regimes': ['trend', 'mean_reversion', 'high_volatility'],
        'confidence_decay_rate': 0.1
    },
    'execution': {
        'enabled': True,
        'binance': {
            'api_key': '${BINANCE_API_KEY}',
            'api_secret': '${BINANCE_API_SECRET}',
            'testnet': True
        },
        'max_slippage': 0.005
    },
    'feedback_loops': {
        'primary_interval': 0.1,
        'secondary_interval': 60,
        'tertiary_interval': 3600
    },
    'logging': {
        'level': 'INFO',
        'format': '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    },
    'database': {
        'url': '${DATABASE_URL}',
        'pool_size': 5
    }
}