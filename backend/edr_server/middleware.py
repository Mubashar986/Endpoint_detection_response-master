import gzip
import zstandard as zstd
from django.utils.deprecation import MiddlewareMixin

class DecompressMiddleware(MiddlewareMixin):
    """
    Middleware to decompress request body if Content-Encoding is gzip or zstd.
    """
    def process_request(self, request):
        # Only process/log for telemetry endpoint to reduce noise
        if request.path == '/api/v1/telemetry/':
            encoding = request.META.get('HTTP_CONTENT_ENCODING', '').lower()
            auth_header = request.META.get('HTTP_AUTHORIZATION', 'None')
            
            if 'zstd' in encoding or 'zstandard' in encoding:
                try:
                    compressed_size = len(request.body)
                    print(f"[Middleware] Received Auth Header: '{auth_header}'")
                    print(f"[Middleware] Decompressing {compressed_size} bytes (Zstd)...")
                    
                    dctx = zstd.ZstdDecompressor()
                    request._body = dctx.decompress(request.body)
                    decompressed_size = len(request._body)
                    
                    print(f"[Middleware] ✅ Decompressed {compressed_size} → {decompressed_size} bytes (saved {((decompressed_size - compressed_size) / decompressed_size * 100):.1f}%)")
                    
                    # Remove the header so other middleware/views don't get confused
                    request.META.pop('HTTP_CONTENT_ENCODING')
                except Exception as e:
                    print(f"[Middleware] Zstd Decompression failed: {e}")
            elif 'gzip' in encoding:
                try:
                    print(f"[Middleware] Received Auth Header: '{auth_header}'")
                    print(f"[Middleware] Decompressing {len(request.body)} bytes (Gzip)...")
                    
                    request._body = gzip.decompress(request.body)
                    
                    # Remove the header so other middleware/views don't get confused
                    request.META.pop('HTTP_CONTENT_ENCODING')
                except Exception as e:
                    print(f"[Middleware] Gzip Decompression failed: {e}")
            else:
                # Only log "Not Compressed" if it's hitting the telemetry endpoint
                print(f"[Middleware] Telemetry request not compressed. Auth: '{auth_header}'")
        
        return None
