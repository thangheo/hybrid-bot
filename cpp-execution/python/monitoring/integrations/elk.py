import logging
import asyncio
import json
import time
from typing import Dict, List, Any, Optional, Union
import aiohttp
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

class ElkClient:
    """
    Client for interacting with the ELK Stack (Elasticsearch, Logstash, Kibana).
    """
    
    def __init__(self, config=None):
        """
        Initialize the ELK client.
        
        Args:
            config: Configuration dictionary for the ELK client
        """
        self.config = config or {}
        self.es_url = self.config.get('elasticsearch_url', 'http://localhost:9200')
        self.kibana_url = self.config.get('kibana_url', 'http://localhost:5601')
        self.logstash_url = self.config.get('logstash_url', 'http://localhost:9600')
        self.username = self.config.get('username', '')
        self.password = self.config.get('password', '')
        self.verify_ssl = self.config.get('verify_ssl', True)
        self.session = None
        self.index_prefix = self.config.get('index_prefix', 'trading-')
        self.default_index = self.config.get('default_index', 'trading-logs')
    
    async def start(self) -> None:
        """Initialize the client session."""
        self.session = aiohttp.ClientSession()
        
        # Test connection to Elasticsearch
        try:
            await self.get_es_health()
            logger.info("Successfully connected to Elasticsearch")
        except Exception as e:
            logger.warning(f"Could not connect to Elasticsearch: {str(e)}")
    
    async def stop(self) -> None:
        """Close the client session."""
        if self.session:
            await self.session.close()
            self.session = None
    
    async def _es_request(self, method: str, path: str, data: Any = None, 
                       params: Dict[str, Any] = None) -> Dict[str, Any]:
        """
        Send a request to the Elasticsearch API.
        
        Args:
            method: HTTP method
            path: API path
            data: Request data
            params: Query parameters
            
        Returns:
            Response data
        """
        if not self.session:
            await self.start()
        
        headers = {'Content-Type': 'application/json'}
        auth = None
        
        # Use basic auth if credentials are provided
        if self.username and self.password:
            auth = aiohttp.BasicAuth(self.username, self.password)
        
        url = f"{self.es_url}/{path.lstrip('/')}"
        
        try:
            async with self.session.request(
                method, 
                url, 
                json=data, 
                params=params, 
                headers=headers,
                auth=auth,
                ssl=self.verify_ssl
            ) as response:
                if response.status >= 400:
                    text = await response.text()
                    raise Exception(f"Elasticsearch API error ({response.status}): {text}")
                
                return await response.json()
        except aiohttp.ClientError as e:
            logger.error(f"HTTP error in Elasticsearch API request: {str(e)}")
            raise
    
    async def _kibana_request(self, method: str, path: str, data: Any = None, 
                           params: Dict[str, Any] = None) -> Dict[str, Any]:
        """
        Send a request to the Kibana API.
        
        Args:
            method: HTTP method
            path: API path
            data: Request data
            params: Query parameters
            
        Returns:
            Response data
        """
        if not self.session:
            await self.start()
        
        headers = {'Content-Type': 'application/json', 'kbn-xsrf': 'true'}
        auth = None
        
        # Use basic auth if credentials are provided
        if self.username and self.password:
            auth = aiohttp.BasicAuth(self.username, self.password)
        
        url = f"{self.kibana_url}/api/{path.lstrip('/')}"
        
        try:
            async with self.session.request(
                method, 
                url, 
                json=data, 
                params=params, 
                headers=headers,
                auth=auth,
                ssl=self.verify_ssl
            ) as response:
                if response.status >= 400:
                    text = await response.text()
                    raise Exception(f"Kibana API error ({response.status}): {text}")
                
                return await response.json()
        except aiohttp.ClientError as e:
            logger.error(f"HTTP error in Kibana API request: {str(e)}")
            raise
    
    async def get_es_health(self) -> Dict[str, Any]:
        """
        Get Elasticsearch cluster health.
        
        Returns:
            Health status
        """
        return await self._es_request('GET', '_cluster/health')
    
    async def get_indices(self) -> List[Dict[str, Any]]:
        """
        Get all Elasticsearch indices.
        
        Returns:
            List of indices
        """
        result = await self._es_request('GET', '_cat/indices?format=json')
        return result
    
    async def create_index(self, index_name: str, mappings: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Create a new Elasticsearch index.
        
        Args:
            index_name: Index name
            mappings: Optional index mappings
            
        Returns:
            Creation result
        """
        data = {}
        
        if mappings:
            data['mappings'] = mappings
        
        return await self._es_request('PUT', index_name, data=data)
    
    async def index_document(self, index_name: str, document: Dict[str, Any], 
                          doc_id: Optional[str] = None) -> Dict[str, Any]:
        """
        Index a document in Elasticsearch.
        
        Args:
            index_name: Index name
            document: Document to index
            doc_id: Optional document ID
            
        Returns:
            Indexing result
        """
        path = f"{index_name}/_doc"
        if doc_id:
            path = f"{index_name}/_doc/{doc_id}"
        
        return await self._es_request('POST', path, data=document)
    
    async def bulk_index(self, index_name: str, documents: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Index multiple documents in bulk.
        
        Args:
            index_name: Index name
            documents: List of documents to index
            
        Returns:
            Bulk indexing result
        """
        # Prepare bulk request body
        bulk_data = []
        
        for doc in documents:
            # Add index action
            bulk_data.append(json.dumps({"index": {"_index": index_name}}))
            # Add document
            bulk_data.append(json.dumps(doc))
        
        # Join with newlines and add trailing newline
        body = "\n".join(bulk_data) + "\n"
        
        headers = {'Content-Type': 'application/x-ndjson'}
        auth = None
        
        # Use basic auth if credentials are provided
        if self.username and self.password:
            auth = aiohttp.BasicAuth(self.username, self.password)
        
        url = f"{self.es_url}/_bulk"
        
        try:
            async with self.session.post(
                url, 
                data=body, 
                headers=headers,
                auth=auth,
                ssl=self.verify_ssl
            ) as response:
                if response.status >= 400:
                    text = await response.text()
                    raise Exception(f"Elasticsearch bulk API error ({response.status}): {text}")
                
                return await response.json()
        except aiohttp.ClientError as e:
            logger.error(f"HTTP error in Elasticsearch bulk API request: {str(e)}")
            raise
    
    async def search(self, index_name: str, query: Dict[str, Any], size: int = 100, 
                  from_: int = 0) -> Dict[str, Any]:
        """
        Search for documents in Elasticsearch.
        
        Args:
            index_name: Index name
            query: Elasticsearch query
            size: Number of results to return
            from_: Starting offset
            
        Returns:
            Search results
        """
        params = {
            'size': size,
            'from': from_
        }
        
        return await self._es_request('POST', f"{index_name}/_search", data=query, params=params)
    
    async def query_logs(self, index_pattern: Optional[str] = None, 
                      query_string: str = "*", 
                      start_time: Optional[Union[datetime, float]] = None,
                      end_time: Optional[Union[datetime, float]] = None,
                      size: int = 100, 
                      sort_field: str = "@timestamp",
                      sort_order: str = "desc",
                      fields: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Query logs with a more user-friendly interface.
        
        Args:
            index_pattern: Index pattern to search
            query_string: Lucene query string
            start_time: Start time for the search
            end_time: End time for the search
            size: Number of results to return
            sort_field: Field to sort by
            sort_order: Sort order ("asc" or "desc")
            fields: Optional list of fields to return
            
        Returns:
            Search results
        """
        if index_pattern is None:
            index_pattern = f"{self.index_prefix}*"
        
        # Build query object
        query = {
            "query": {
                "bool": {
                    "must": [
                        {
                            "query_string": {
                                "query": query_string
                            }
                        }
                    ]
                }
            },
            "sort": [
                {
                    sort_field: {
                        "order": sort_order
                    }
                }
            ]
        }
        
        # Add time range if provided
        if start_time or end_time:
            range_query = {"range": {"@timestamp": {}}}
            
            if start_time:
                if isinstance(start_time, datetime):
                    start_time = start_time.isoformat()
                elif isinstance(start_time, (int, float)):
                    start_time = datetime.fromtimestamp(start_time).isoformat()
                
                range_query["range"]["@timestamp"]["gte"] = start_time
            
            if end_time:
                if isinstance(end_time, datetime):
                    end_time = end_time.isoformat()
                elif isinstance(end_time, (int, float)):
                    end_time = datetime.fromtimestamp(end_time).isoformat()
                
                range_query["range"]["@timestamp"]["lte"] = end_time
            
            query["query"]["bool"]["must"].append(range_query)
        
        # Add specific fields if requested
        if fields:
            query["_source"] = fields
        
        return await self.search(index_pattern, query, size=size)
    
    async def store_log_entry(self, log_entry: Dict[str, Any], index_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Store a log entry in Elasticsearch.
        
        Args:
            log_entry: Log entry to store
            index_name: Optional index name
            
        Returns:
            Indexing result
        """
        if index_name is None:
            index_name = self.default_index
        
        # Ensure the log entry has a timestamp
        if '@timestamp' not in log_entry:
            log_entry['@timestamp'] = datetime.now().isoformat()
        
        return await self.index_document(index_name, log_entry)
    
    async def store_metrics(self, metrics: Dict[str, Any], index_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Store metrics in Elasticsearch.
        
        Args:
            metrics: Metrics to store
            index_name: Optional index name
            
        Returns:
            Indexing result
        """
        if index_name is None:
            index_name = f"{self.index_prefix}metrics-{datetime.now().strftime('%Y.%m.%d')}"
        
        # Ensure the metrics have a timestamp
        if '@timestamp' not in metrics:
            metrics['@timestamp'] = datetime.now().isoformat()
        
        return await self.index_document(index_name, metrics)
    
    async def store_log_entries(self, log_entries: List[Dict[str, Any]], 
                             index_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Store multiple log entries in Elasticsearch.
        
        Args:
            log_entries: List of log entries to store
            index_name: Optional index name
            
        Returns:
            Bulk indexing result
        """
        if index_name is None:
            index_name = self.default_index
        
        # Ensure each log entry has a timestamp
        for entry in log_entries:
            if '@timestamp' not in entry:
                entry['@timestamp'] = datetime.now().isoformat()
        
        return await self.bulk_index(index_name, log_entries)
    
    async def create_kibana_index_pattern(self, pattern: str, time_field: str = "@timestamp") -> Dict[str, Any]:
        """
        Create a Kibana index pattern.
        
        Args:
            pattern: Index pattern
            time_field: Timestamp field name
            
        Returns:
            Creation result
        """
        data = {
            "index_pattern": {
                "title": pattern,
                "timeFieldName": time_field
            }
        }
        
        return await self._kibana_request('POST', 'index_patterns/index_pattern', data=data)
    
    async def create_kibana_visualization(self, title: str, visualization_type: str, 
                                       index_pattern: str, visualization_state: Dict[str, Any]) -> Dict[str, Any]:
        """
        Create a Kibana visualization.
        
        Args:
            title: Visualization title
            visualization_type: Visualization type (e.g., "line", "bar", "area")
            index_pattern: Index pattern to use
            visualization_state: Visualization state configuration
            
        Returns:
            Creation result
        """
        data = {
            "type": "visualization",
            "attributes": {
                "title": title,
                "visState": json.dumps({
                    "title": title,
                    "type": visualization_type,
                    "params": visualization_state,
                    "aggs": [],
                    "indexPattern": index_pattern
                }),
                "kibanaSavedObjectMeta": {
                    "searchSourceJSON": json.dumps({
                        "index": index_pattern,
                        "filter": [],
                        "query": {"query": "", "language": "kuery"}
                    })
                }
            }
        }
        
        return await self._kibana_request('POST', 'saved_objects/visualization', data=data)
    
    async def create_kibana_dashboard(self, title: str, description: str = "", 
                                   panels: List[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Create a Kibana dashboard.
        
        Args:
            title: Dashboard title
            description: Dashboard description
            panels: List of dashboard panels
            
        Returns:
            Creation result
        """
        data = {
            "type": "dashboard",
            "attributes": {
                "title": title,
                "description": description,
                "version": 1,
                "panelsJSON": json.dumps(panels or [])
            }
        }
        
        return await self._kibana_request('POST', 'saved_objects/dashboard', data=data)
    
    async def create_system_monitoring_dashboard(self) -> Dict[str, Any]:
        """
        Create a pre-configured system monitoring dashboard in Kibana.
        
        Returns:
            Creation result
        """
        # Create index pattern
        await self.create_kibana_index_pattern(f"{self.index_prefix}metrics-*")
        
        # Create visualizations
        cpu_viz = await self.create_kibana_visualization(
            "CPU Utilization",
            "line",
            f"{self.index_prefix}metrics-*",
            {
                "grid": {"categoryLines": False},
                "categoryAxes": [
                    {"id": "CategoryAxis-1", "type": "category", "position": "bottom", "title": {}}
                ],
                "valueAxes": [
                    {"id": "ValueAxis-1", "name": "LeftAxis-1", "type": "value", "position": "left", 
                     "title": {"text": "CPU Utilization %"}}
                ],
                "seriesParams": [
                    {"show": "true", "type": "line", "mode": "normal", "data": {"label": "CPU %", "id": "1"}}
                ]
            }
        )
        
        memory_viz = await self.create_kibana_visualization(
            "Memory Utilization",
            "line",
            f"{self.index_prefix}metrics-*",
            {
                "grid": {"categoryLines": False},
                "categoryAxes": [
                    {"id": "CategoryAxis-1", "type": "category", "position": "bottom", "title": {}}
                ],
                "valueAxes": [
                    {"id": "ValueAxis-1", "name": "LeftAxis-1", "type": "value", "position": "left", 
                     "title": {"text": "Memory Utilization %"}}
                ],
                "seriesParams": [
                    {"show": "true", "type": "line", "mode": "normal", "data": {"label": "Memory %", "id": "1"}}
                ]
            }
        )
        
        disk_viz = await self.create_kibana_visualization(
            "Disk Utilization",
            "line",
            f"{self.index_prefix}metrics-*",
            {
                "grid": {"categoryLines": False},
                "categoryAxes": [
                    {"id": "CategoryAxis-1", "type": "category", "position": "bottom", "title": {}}
                ],
                "valueAxes": [
                    {"id": "ValueAxis-1", "name": "LeftAxis-1", "type": "value", "position": "left", 
                     "title": {"text": "Disk Utilization %"}}
                ],
                "seriesParams": [
                    {"show": "true", "type": "line", "mode": "normal", "data": {"label": "Disk %", "id": "1"}}
                ]
            }
        )
        
        # Create dashboard with visualizations
        dashboard = await self.create_kibana_dashboard(
            "System Monitoring",
            "System resource utilization dashboard",
            [
                {
                    "panelIndex": "1",
                    "gridData": {"x": 0, "y": 0, "w": 24, "h": 8, "i": "1"},
                    "id": cpu_viz["id"],
                    "type": "visualization"
                },
                {
                    "panelIndex": "2",
                    "gridData": {"x": 0, "y": 8, "w": 12, "h": 8, "i": "2"},
                    "id": memory_viz["id"],
                    "type": "visualization"
                },
                {
                    "panelIndex": "3",
                    "gridData": {"x": 12, "y": 8, "w": 12, "h": 8, "i": "3"},
                    "id": disk_viz["id"],
                    "type": "visualization"
                }
            ]
        )
        
        return dashboard