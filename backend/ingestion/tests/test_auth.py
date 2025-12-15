import pytest
from unittest.mock import Mock, patch, MagicMock
from ingestion.auth import IsAgentAuthenticated, AgentTokenAuthentication, IsAgentOrAuthenticated
from ingestion.models_mongo import Agent


class TestIsAgentAuthenticated:
    """Tests for IsAgentAuthenticated permission class"""
    
    def test_agent_auth_success(self):
        """Agent with valid token should pass"""
        request = Mock()
        request.auth = Agent(agent_id="test-uuid")
        request.user = None
        
        perm = IsAgentAuthenticated()
        assert perm.has_permission(request, None) == True
    
    def test_user_auth_success(self):
        """Dashboard user should also pass"""
        request = Mock()
        request.auth = None
        request.user = Mock()
        request.user.is_authenticated = True
        
        perm = IsAgentAuthenticated()
        assert perm.has_permission(request, None) == True
    
    def test_no_auth_fails(self):
        """Unauthenticated request should fail"""
        request = Mock()
        request.auth = None
        request.user = Mock()
        request.user.is_authenticated = False
        
        perm = IsAgentAuthenticated()
        assert perm.has_permission(request, None) == False


class TestIsAgentAuthenticatedEdgeCases:
    """Edge case tests for IsAgentAuthenticated"""
    
    def test_none_user_with_none_auth(self):
        """Both user and auth None should fail"""
        request = Mock()
        request.auth = None
        request.user = None
        
        perm = IsAgentAuthenticated()
        assert perm.has_permission(request, None) == False
    
    def test_auth_not_agent_object(self):
        """Non-Agent auth object should fallback to user check"""
        request = Mock()
        request.auth = "not-an-agent"  # String instead of Agent
        request.user = Mock()
        request.user.is_authenticated = False
        
        perm = IsAgentAuthenticated()
        # Should fail because auth is not Agent and user not authenticated
        assert perm.has_permission(request, None) == False
    
    def test_auth_is_dict_not_agent(self):
        """Dict auth object should fallback to user check"""
        request = Mock()
        request.auth = {"token": "abc"}  # Dict instead of Agent
        request.user = Mock()
        request.user.is_authenticated = True
        
        perm = IsAgentAuthenticated()
        # Should pass because user is authenticated
        assert perm.has_permission(request, None) == True
    
    def test_empty_agent_id(self):
        """Agent with empty agent_id should still pass (it's still Agent type)"""
        request = Mock()
        request.auth = Agent(agent_id="")  # Empty but valid Agent
        request.user = None
        
        perm = IsAgentAuthenticated()
        assert perm.has_permission(request, None) == True


class TestAgentTokenAuthentication:
    """Tests for AgentTokenAuthentication class"""
    
    def test_missing_auth_header(self):
        """Missing Authorization header should return None"""
        request = Mock()
        request.headers = {}
        
        auth = AgentTokenAuthentication()
        assert auth.authenticate(request) is None
    
    def test_wrong_auth_type(self):
        """Token (not AgentToken) prefix should return None"""
        request = Mock()
        request.headers = {"Authorization": "Token abc123"}
        
        auth = AgentTokenAuthentication()
        assert auth.authenticate(request) is None
    
    def test_bearer_auth_type(self):
        """Bearer prefix should return None"""
        request = Mock()
        request.headers = {"Authorization": "Bearer abc123"}
        
        auth = AgentTokenAuthentication()
        assert auth.authenticate(request) is None
    
    def test_empty_token_after_prefix(self):
        """AgentToken with no token should raise error"""
        from rest_framework.exceptions import AuthenticationFailed
        
        request = Mock()
        request.headers = {"Authorization": "AgentToken "}
        
        auth = AgentTokenAuthentication()
        with pytest.raises(AuthenticationFailed):
            auth.authenticate(request)
    
    def test_short_token(self):
        """Token less than 64 chars should raise error"""
        from rest_framework.exceptions import AuthenticationFailed
        
        request = Mock()
        request.headers = {"Authorization": "AgentToken abc123"}
        
        auth = AgentTokenAuthentication()
        with pytest.raises(AuthenticationFailed):
            auth.authenticate(request)
    
    def test_long_token(self):
        """Token more than 64 chars should raise error"""
        from rest_framework.exceptions import AuthenticationFailed
        
        request = Mock()
        token = "a" * 100
        request.headers = {"Authorization": f"AgentToken {token}"}
        
        auth = AgentTokenAuthentication()
        with pytest.raises(AuthenticationFailed):
            auth.authenticate(request)
    
    def test_correct_length_invalid_token(self):
        """64-char token not in DB should raise error"""
        from rest_framework.exceptions import AuthenticationFailed
        
        request = Mock()
        token = "a" * 64  # Correct length but not in database
        request.headers = {"Authorization": f"AgentToken {token}"}
        
        auth = AgentTokenAuthentication()
        
        # Mock the database lookup to simulate "not found"
        # This avoids needing a real MongoDB connection
        with patch.object(Agent, 'objects') as mock_objects:
            mock_objects.get.side_effect = Agent.DoesNotExist()
            
            with pytest.raises(AuthenticationFailed):
                auth.authenticate(request)


class TestIsAgentOrAuthenticated:
    """Tests for IsAgentOrAuthenticated permission class"""
    
    def test_agent_passes(self):
        """Agent auth should pass"""
        request = Mock()
        request.auth = Agent(agent_id="test")
        request.user = None
        
        perm = IsAgentOrAuthenticated()
        assert perm.has_permission(request, None) == True
    
    def test_user_passes(self):
        """User auth should pass"""
        request = Mock()
        request.auth = None
        request.user = Mock()
        request.user.is_authenticated = True
        
        perm = IsAgentOrAuthenticated()
        assert perm.has_permission(request, None) == True
    
    def test_neither_fails(self):
        """No auth should fail"""
        request = Mock()
        request.auth = None
        request.user = Mock()
        request.user.is_authenticated = False
        
        perm = IsAgentOrAuthenticated()
        assert perm.has_permission(request, None) == False


# ============================================================================
# INTEGRATION TESTS - Real API Testing
# ============================================================================
# These tests require:
#   1. Django server running: python manage.py runserver
#   2. MongoDB running
#   3. Valid admin credentials
#
# Run with: pytest ingestion/tests/test_auth.py::TestRealIntegration -v
# ============================================================================

import requests
import os

class TestRealIntegration:
    """
    Real integration tests that hit actual API endpoints.
    
    PREREQUISITES:
    - Django server running on localhost:8000
    - MongoDB running
    - Admin user created with known credentials
    
    Set environment variables:
    - EDR_TEST_SERVER=http://localhost:8000
    - EDR_ADMIN_TOKEN=<your-admin-token>
    """
    
    BASE_URL = os.environ.get('EDR_TEST_SERVER', 'http://localhost:8000')
    ADMIN_TOKEN = os.environ.get('EDR_ADMIN_TOKEN', '')
    
    @pytest.fixture
    def admin_headers(self):
        """Headers for admin API calls"""
        return {
            'Authorization': f'Token {self.ADMIN_TOKEN}',
            'Content-Type': 'application/json'
        }
    
    @pytest.mark.skipif(
        not os.environ.get('EDR_ADMIN_TOKEN'),
        reason="Set EDR_ADMIN_TOKEN to run integration tests"
    )
    def test_01_generate_enrollment_token(self, admin_headers):
        """Generate a new enrollment token via API"""
        response = requests.post(
            f'{self.BASE_URL}/api/v1/tokens/',
            headers=admin_headers,
            json={'max_uses': 5, 'expires_hours': 24}
        )
        
        assert response.status_code == 201, f"Failed: {response.text}"
        data = response.json()
        assert 'token' in data
        assert len(data['token']) == 64
        
        # Store for subsequent tests
        os.environ['EDR_TEST_ENROLLMENT_TOKEN'] = data['token']
        print(f"\n✅ Generated enrollment token: {data['token'][:16]}...")
    
    @pytest.mark.skipif(
        not os.environ.get('EDR_ADMIN_TOKEN'),
        reason="Set EDR_ADMIN_TOKEN to run integration tests"
    )
    def test_02_enroll_agent(self):
        """Enroll a test agent"""
        enrollment_token = os.environ.get('EDR_TEST_ENROLLMENT_TOKEN')
        if not enrollment_token:
            pytest.skip("No enrollment token - run test_01 first")
        
        import uuid
        agent_id = str(uuid.uuid4())
        
        response = requests.post(
            f'{self.BASE_URL}/api/v1/enroll/',
            json={
                'enrollment_token': enrollment_token,
                'agent_id': agent_id,
                'hostname': 'TEST-PC-INTEGRATION',
                'os_version': 'Windows 10 Test',
                'agent_version': '1.0.0'
            }
        )
        
        assert response.status_code == 201, f"Failed: {response.text}"
        data = response.json()
        assert 'identity_token' in data
        assert len(data['identity_token']) == 64
        
        # Store for subsequent tests
        os.environ['EDR_TEST_IDENTITY_TOKEN'] = data['identity_token']
        os.environ['EDR_TEST_AGENT_ID'] = agent_id
        print(f"\n✅ Agent enrolled: {agent_id[:16]}...")
        print(f"   Identity token: {data['identity_token'][:16]}...")
    
    @pytest.mark.skipif(
        not os.environ.get('EDR_ADMIN_TOKEN'),
        reason="Set EDR_ADMIN_TOKEN to run integration tests"
    )
    def test_03_send_telemetry(self):
        """Send telemetry with agent token"""
        identity_token = os.environ.get('EDR_TEST_IDENTITY_TOKEN')
        agent_id = os.environ.get('EDR_TEST_AGENT_ID')
        if not identity_token:
            pytest.skip("No identity token - run test_02 first")
        
        import uuid
        from datetime import datetime
        
        response = requests.post(
            f'{self.BASE_URL}/api/v1/telemetry/',
            headers={
                'Authorization': f'AgentToken {identity_token}',
                'Content-Type': 'application/json',
                'X-Agent-Token': identity_token
            },
            json={
                'event_id': str(uuid.uuid4()),
                'agent_id': agent_id,
                'event_type': 1,
                'timestamp': datetime.utcnow().isoformat() + 'Z',
                'data': {'test': True, 'source': 'integration_test'}
            }
        )
        
        assert response.status_code == 201, f"Failed ({response.status_code}): {response.text}"
        print(f"\n✅ Telemetry sent successfully")
    
    @pytest.mark.skipif(
        not os.environ.get('EDR_ADMIN_TOKEN'),
        reason="Set EDR_ADMIN_TOKEN to run integration tests"
    )
    def test_04_send_heartbeat(self):
        """Send heartbeat with agent token"""
        identity_token = os.environ.get('EDR_TEST_IDENTITY_TOKEN')
        agent_id = os.environ.get('EDR_TEST_AGENT_ID')
        if not identity_token:
            pytest.skip("No identity token - run test_02 first")
        
        response = requests.post(
            f'{self.BASE_URL}/api/v1/heartbeat/',
            headers={
                'Authorization': f'AgentToken {identity_token}',
                'Content-Type': 'application/json'
            },
            json={
                'agent_id': agent_id,
                'agent_version': '1.0.0',
                'hostname': 'TEST-PC-INTEGRATION',
                'status': 'running',
                'cpu_percent': 25.5,
                'memory_percent': 45.0
            }
        )
        
        assert response.status_code == 200, f"Failed ({response.status_code}): {response.text}"
        print(f"\n✅ Heartbeat sent successfully")
    
    @pytest.mark.skipif(
        not os.environ.get('EDR_ADMIN_TOKEN'),
        reason="Set EDR_ADMIN_TOKEN to run integration tests"
    )
    def test_05_poll_commands(self):
        """Poll for commands with agent token"""
        identity_token = os.environ.get('EDR_TEST_IDENTITY_TOKEN')
        agent_id = os.environ.get('EDR_TEST_AGENT_ID')
        if not identity_token:
            pytest.skip("No identity token - run test_02 first")
        
        response = requests.get(
            f'{self.BASE_URL}/api/v1/commands/poll/',
            headers={
                'Authorization': f'AgentToken {identity_token}',
                'X-Agent-ID': agent_id
            }
        )
        
        # 204 = no commands, 200 = command available
        assert response.status_code in [200, 204], f"Failed ({response.status_code}): {response.text}"
        print(f"\n✅ Command poll successful (status: {response.status_code})")
