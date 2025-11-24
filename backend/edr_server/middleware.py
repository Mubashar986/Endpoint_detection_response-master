import gzip
from django.utils.deprecation import MiddlewareMixin

class DecompressMiddleware(MiddlewareMixin):
    """
    Middleware to decompress request body if Content-Encoding is gzip.
    """
    def process_request(self, request):
        # Only process/log for telemetry endpoint to reduce noise
        if request.path == '/api/v1/telemetry/':
            encoding = request.META.get('HTTP_CONTENT_ENCODING', '').lower()
            auth_header = request.META.get('HTTP_AUTHORIZATION', 'None')
            
            if 'gzip' in encoding:
                try:
                    print(f"[Middleware] Received Auth Header: '{auth_header}'")
                    print(f"[Middleware] Decompressing {len(request.body)} bytes...")
                    
                    request._body = gzip.decompress(request.body)
                    
                    # Remove the header so other middleware/views don't get confused
                    request.META.pop('HTTP_CONTENT_ENCODING')
                except Exception as e:
                    print(f"[Middleware] Decompression failed: {e}")
            else:
                # Only log "Not Gzip" if it's hitting the telemetry endpoint
                # This helps us spot if the agent forgot to send the header
                print(f"[Middleware] Telemetry request not Gzip. Auth: '{auth_header}'")
        
        return None
