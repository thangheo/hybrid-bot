# Data contracts between layers
@dataclass
class LayerOutput:
    layer_id: str
    data: Any
    timestamp: float
    confidence: float
    metadata: Dict[str, Any]

@dataclass
class LayerInput:
    source_layer: str
    data: Any
    processing_priority: int
    timeout_ms: int